#include "elf.h"
#include "dynlink.h"
#include "printk.h"
#include "vmm.h"
#include "pmm.h"
#include "sched.h"
#include "string.h"
#include "slab.h"
#include "vfs.h"

/* Bounds on how many user pages one image may claim, so the per-page
   protection bookkeeping stays small even for hostile headers. */
#define ELF_MAX_SEG_PAGES (1u << 20)

/* Per-page coverage bits used to derive W^X protections. */
#define SEG_COVER_W 1
#define SEG_COVER_X 2

#define ELF_MAX_IMAGES 8
#define ELF_MAX_DEP_NAME 64

/* Where on the root partition a DT_NEEDED library (e.g. "libc.sl") may
   live. Tried in order; the image build plants libc.sl under /Libraries. */
static const char *const elf_lib_dirs[] = { "/Libraries/", "/Binaries/", "/bin/", "/", 0 };

/* Runtime image bookkeeping: the mapped segments plus the dynamic-link
   description of the image's ELF bytes (kept in kernel memory). */
struct elf_image {
    uint64_t span_lo;
    size_t span_pages;
    uint8_t *cover;                 /* W^(X) coverage bits per page */
    uint64_t highest;               /* end of mapped segments */
    int owns_buf;                   /* loader owns dyn.buf, free after reloc */
    char name[32];
    struct elf_object dyn;          /* points at the image file bytes */
};

static void elf_free_image(struct elf_image *img)
{
    if (img->cover)
    {
        kfree(img->cover);
        img->cover = 0;
    }
}

/* Release the loader-owned file-byte buffers after relocation, when the
   dynamic tables they back are no longer needed. */
static void elf_free_owned_buffers(struct elf_image *imgs, size_t nimg)
{
    for (size_t o = 0; o < nimg; o++)
    {
        if (imgs[o].owns_buf)
        {
            kfree((void *)(uintptr_t)imgs[o].dyn.buf);
            imgs[o].owns_buf = 0;
        }
    }
}

static int elf_valid(const struct elf64_ehdr *h)
{
    if (h->ei_magic != ELF_MAGIC)
        return 0;
    if (h->ei_class != ELFCLASS64)
        return 0;
    if (h->ei_data != ELFDATA2LSB)
        return 0;
    if (h->e_machine != EM_X86_64)
        return 0;
    if (h->e_phnum == 0)
        return 0;
    return 1;
}

/* Map every PT_LOAD segment of `data` into `mmu` (which must already be
   the active address space) and record the per-page protection coverage.
   Pages are left writable so the relocation pass can fill GOT/PLT slots;
   elf_load make_images_readonly() applies the final W^X protections. */
static int elf_map_image(struct mmu_root *mmu, const uint8_t *data, size_t size,
                         struct elf_image *img, int require_entry)
{
    memset(img, 0, sizeof(*img));
    img->dyn.buf = data;
    img->dyn.size = size;

    if (size < sizeof(struct elf64_ehdr))
        return -1;

    const struct elf64_ehdr *h = (const struct elf64_ehdr *)data;
    if (!elf_valid(h))
    {
        printk("ELF(%s): invalid header\n", img->name);
        return -1;
    }
    if (h->e_phentsize != sizeof(struct elf64_phdr) ||
        h->e_phoff > size ||
        (uint64_t)h->e_phnum > (size - h->e_phoff) / sizeof(struct elf64_phdr))
    {
        printk("ELF(%s): invalid program headers\n", img->name);
        return -1;
    }

    const struct elf64_phdr *ph = (const struct elf64_phdr *)(data + h->e_phoff);

    if (require_entry)
    {
        int entry_ok = 0;
        for (uint16_t i = 0; i < h->e_phnum; i++)
        {
            if (ph[i].p_type != PT_LOAD)
                continue;
            if ((ph[i].p_flags & PF_X) && h->e_entry >= ph[i].p_vaddr &&
                h->e_entry < ph[i].p_vaddr + ph[i].p_memsz)
                entry_ok = 1;
        }
        if (!entry_ok)
        {
            printk("ELF(%s): entry outside executable segment\n", img->name);
            return -1;
        }
    }

    /* Compute the union of all PT_LOAD pages so per-page protections can be
       applied. A page shared by an executable and a writable segment must
       stay writable, and therefore non-executable. */
    uint64_t span_lo = UINT64_MAX;
    uint64_t span_hi = 0;
    for (uint16_t i = 0; i < h->e_phnum; i++)
    {
        if (ph[i].p_type != PT_LOAD)
            continue;
        if (ph[i].p_filesz > ph[i].p_memsz || ph[i].p_offset > size ||
            ph[i].p_filesz > size - ph[i].p_offset ||
            ph[i].p_vaddr + ph[i].p_memsz < ph[i].p_vaddr)
        {
            printk("ELF(%s): invalid segment\n", img->name);
            return -1;
        }
        uint64_t vlo = ph[i].p_vaddr & ~0xFFFULL;
        uint64_t vhi = (ph[i].p_vaddr + ph[i].p_memsz + 0xFFFULL) & ~0xFFFULL;
        if (vlo < span_lo) span_lo = vlo;
        if (vhi > span_hi) span_hi = vhi;
    }
    uint64_t span_pages = (span_hi - span_lo) / PAGE_SIZE;
    if (span_pages > ELF_MAX_SEG_PAGES)
    {
        printk("ELF(%s): segment span too large (%lu pages)\n",
               img->name, (unsigned long)span_pages);
        return -1;
    }
    uint8_t *cover = kmalloc(span_pages ? (size_t)span_pages : 1);
    if (!cover)
        return -1;
    memset(cover, 0, span_pages ? (size_t)span_pages : 1);

    for (uint16_t i = 0; i < h->e_phnum; i++)
    {
        if (ph[i].p_type != PT_LOAD)
            continue;
        uint64_t vstart = ph[i].p_vaddr & ~0xFFFULL;
        uint64_t vend   = (ph[i].p_vaddr + ph[i].p_memsz + 0xFFFULL) & ~0xFFFULL;
        for (uint64_t va = vstart; va < vend; va += PAGE_SIZE)
        {
            uint64_t idx = (va - span_lo) / PAGE_SIZE;
            if (ph[i].p_flags & PF_W)
                cover[idx] |= SEG_COVER_W;
            if (ph[i].p_flags & PF_X)
                cover[idx] |= SEG_COVER_X;
        }
    }

    uint64_t highest = 0;
    for (uint16_t i = 0; i < h->e_phnum; i++)
    {
        if (ph[i].p_type != PT_LOAD)
            continue;

        uint64_t vaddr  = ph[i].p_vaddr;
        uint64_t filesz = ph[i].p_filesz;
        uint64_t memsz  = ph[i].p_memsz;
        uint64_t offset = ph[i].p_offset;

        uint64_t vstart = vaddr & ~0xFFFULL;
        uint64_t vend   = (vaddr + memsz + 0xFFFULL) & ~0xFFFULL;
        uint64_t pages  = (vend - vstart) / PAGE_SIZE;

        for (uint64_t p = 0; p < pages; p++)
        {
            uint64_t va = vstart + p * PAGE_SIZE;
            if (vmm_virt_to_phys_in(mmu, va) != 0)
                continue;   /* page shared with a previous segment */

            uint64_t phys = (uint64_t)pmm_alloc_frame();
            if (!phys)
            {
                printk("ELF(%s): OOM mapping 0x%lx\n", img->name, va);
                return -1;
            }
            /* Map writable temporarily so image bytes and relocation data
               can be written; final W^X protections come afterwards. */
            if (vmm_map_page_in(mmu, va, phys, MMU_USER | MMU_WRITE) < 0)
            {
                pmm_free_frame((void *)phys);
                printk("ELF(%s): map failed 0x%lx\n", img->name, va);
                return -1;
            }

            uint64_t copy_off = va - vaddr;
            uint64_t file_off = offset + copy_off;
            uint64_t file_end = offset + filesz;

            if (file_off < file_end)
            {
                uint64_t copy_len = PAGE_SIZE;
                if (file_off + copy_len > file_end)
                    copy_len = file_end - file_off;
                memcpy((void *)va, data + file_off, (size_t)copy_len);
                if (copy_len < PAGE_SIZE)
                    memset((void *)(va + copy_len), 0,
                           PAGE_SIZE - copy_len);
            }
            else
            {
                memset((void *)va, 0, PAGE_SIZE);
            }
        }

        if (vaddr + memsz > highest)
            highest = vaddr + memsz;
    }

    img->span_lo = span_lo;
    img->span_pages = (size_t)span_pages;
    img->cover = cover;
    img->highest = highest;

    if (elf_parse_dynamic(&img->dyn) < 0)
    {
        printk("ELF(%s): bad dynamic data\n", img->name);
        return -1;
    }
    return 0;
}

/* Load the DT_NEEDED libraries of the main image (imgs[0]) into the same
   address space, appending them after it. */
static int elf_load_dependencies(struct mmu_root *mmu, struct elf_image *imgs,
                                 size_t *nimg)
{
    struct elf_image *main = &imgs[0];
    if (!main->dyn.info.has_dynamic)
        return 0;

    const struct elf_dyninfo *di = &main->dyn.info;
    if (di->dynstr_off == 0 && di->nneeded > 0)
        return -1;

    char name[ELF_MAX_DEP_NAME];
    for (int d = 0; d < di->nneeded; d++)
    {
        uint32_t off = di->needed_off[d];
        if (off >= di->dynstr_size)
            return -1;
        const uint8_t *s = main->dyn.buf + di->dynstr_off + off;
        size_t n = 0;
        while (n < sizeof(name) - 1 && s[n])
        {
            name[n] = (char)s[n];
            n++;
        }
        name[n] = 0;
        if (n == 0 || n >= sizeof(name) - 1)
            return -1;

        int loaded = 0;
        for (int dir = 0; elf_lib_dirs[dir]; dir++)
        {
            char path[128];
            size_t pl = 0;
            while (elf_lib_dirs[dir][pl])
            {
                path[pl] = elf_lib_dirs[dir][pl];
                pl++;
            }
            if (pl + n >= sizeof(path))
                return -1;
            memcpy(path + pl, name, n + 1);

            uint8_t *buf = 0;
            size_t sz = 0;
            if (vfs_read_file(path, &buf, &sz) != 0)
                continue;
            if (*nimg >= ELF_MAX_IMAGES)
            {
                printk("DYN: too many images for '%s'\n", name);
                kfree(buf);
                return -1;
            }
            struct elf_image *dep = &imgs[(*nimg)++];
            memset(dep, 0, sizeof(*dep));
            memcpy(dep->name, name, strlen(name) < sizeof(dep->name) ?
                        strlen(name) : sizeof(dep->name) - 1);
            if (elf_map_image(mmu, buf, sz, dep, 0) < 0)
            {
                printk("DYN: failed to map library '%s'\n", path);
                kfree(buf);
                (*nimg)--;
                return -1;
            }
            dep->owns_buf = 1;  /* keep bytes until relocation is applied */
            loaded = 1;
            break;
        }
        if (!loaded)
        {
            printk("DYN: library '%s' not found\n", name);
            return -1;
        }
    }
    return 0;
}

/* The new address space owns these user mappings, but is not yet the active
   page table. Callers switch to it before writing to the user virtual
   addresses; this closure writes one relocated pointer slot. */
static void elf_place_reloc(uint64_t target_va, uint64_t value, void *ctx)
{
    (void)ctx;
    *(volatile uint64_t *)(uintptr_t)target_va = value;
}

/* Apply the final W^X per-page protections for one image. */
static int elf_image_readonly(struct elf_image *img, struct mmu_root *mmu)
{
    for (size_t idx = 0; idx < img->span_pages; idx++)
    {
        uint64_t va = img->span_lo + idx * PAGE_SIZE;
        if (vmm_virt_to_phys_in(mmu, va) == 0)
            continue;
        uint32_t flags = MMU_USER;
        if (img->cover[idx] & SEG_COVER_W)
            flags |= MMU_WRITE;
        if ((img->cover[idx] & SEG_COVER_W) || !(img->cover[idx] & SEG_COVER_X))
            flags |= MMU_NX;
        if (vmm_protect_page(mmu, va, flags) < 0)
        {
            printk("ELF(%s): protect failed 0x%lx\n", img->name, va);
            return -1;
        }
    }
    return 0;
}

static int setup_user_stack(uint64_t stack_base, uint64_t stack_pages,
                            int argc, char **argv, int envc, char **envp,
                            uint64_t *stack_top);

int elf_load(struct mmu_root *mmu, const uint8_t *data, size_t size,
              uint64_t *entry, uint64_t *stack_top, int argc, char **argv,
              int envc, char **envp)
{
    struct elf_image imgs[ELF_MAX_IMAGES];
    memset(imgs, 0, sizeof(imgs));
    size_t nimg = 0;

    /* The new address space must be active to copy image bytes and to apply
       relocations into the freshly mapped pages. */
    struct mmu_root *saved = vmm_current_root();
    vmm_switch(mmu);

    memcpy(imgs[nimg].name, "main", 5);
    if (elf_map_image(mmu, data, size, &imgs[nimg], 1) < 0)
        goto fail;
    nimg++;

    if (elf_load_dependencies(mmu, imgs, &nimg) < 0)
        goto fail;

    /* Relocate everything (eager binding). The scope is the executable
       followed by its shared libraries, so symbols resolve the same way the
       host dynamic linker would. */
    struct elf_object scope[ELF_MAX_IMAGES];
    for (size_t o = 0; o < nimg; o++)
        scope[o] = imgs[o].dyn;
    for (size_t o = 0; o < nimg; o++)
    {
        int applied = 0;
        if (elf_apply_relocations(&imgs[o].dyn, scope, nimg,
                                  elf_place_reloc, 0, &applied) < 0)
        {
            printk("DYN: relocation of '%s' failed\n", imgs[o].name);
            goto fail;
        }
    }

    elf_free_owned_buffers(imgs, nimg);

    for (size_t o = 0; o < nimg; o++)
    {
        if (elf_image_readonly(&imgs[o], mmu) < 0)
            goto fail;
        elf_free_image(&imgs[o]);
    }

    const struct elf64_ehdr *h = (const struct elf64_ehdr *)data;
    *entry = h->e_entry;

    uint64_t highest = 0;
    for (size_t o = 0; o < nimg; o++)
        if (imgs[o].highest > highest)
            highest = imgs[o].highest;

    uint64_t stack_top_ptr = 0;
    uint64_t stack_base = (highest + 0x10000ULL + 0xFFFULL) & ~0xFFFULL;
    uint64_t stack_pages = 32;

    /* Guard page: the page below the mapped stack is left unmapped so a
       downward stack overflow faults instead of silently corrupting. */
    uint64_t stack = stack_base + PAGE_SIZE;

    for (uint64_t p = 0; p < stack_pages; p++)
    {
        uint64_t va   = stack + p * PAGE_SIZE;
        uint64_t phys = (uint64_t)pmm_alloc_frame();
        if (!phys)
        {
            printk("ELF: OOM stack\n");
            goto fail;
        }
        if (vmm_map_page_in(mmu, va, phys, MMU_USER | MMU_WRITE | MMU_NX) < 0)
        {
            pmm_free_frame((void *)phys);
            goto fail;
        }
    }

    if (setup_user_stack(stack, stack_pages, argc, argv, envc, envp,
                         &stack_top_ptr))
        goto fail;

    *stack_top = stack_top_ptr;

    vmm_switch(saved);
    return 0;

fail:
    vmm_switch(saved);
    elf_free_owned_buffers(imgs, nimg);
    for (size_t o = 0; o < nimg; o++)
        elf_free_image(&imgs[o]);
    return -1;
}

/* Build an initial user stack: argc, argv[], NULL, envp[], NULL, with the
   argument and environment strings copied just above the vectors. */
static int setup_user_stack(uint64_t stack_base, uint64_t stack_pages,
                            int argc, char **argv, int envc, char **envp,
                            uint64_t *stack_top)
{
    uint64_t top = stack_base + stack_pages * PAGE_SIZE;
    uint64_t sp = top & ~0xFULL;

    if (argc < 0) argc = 0;
    if (argc > 32) argc = 32;
    if (envc < 0) envc = 0;
    if (envc > 32) envc = 32;

    uint64_t str_ptrs[32];
    for (int i = 0; i < argc; i++)
    {
        const char *s = argv[i] ? argv[i] : "";
        size_t l = strlen(s) + 1;
        sp -= l;
        memcpy((void *)(uintptr_t)sp, s, l);
        str_ptrs[i] = sp;
    }
    uint64_t env_ptrs[32];
    for (int i = 0; i < envc; i++)
    {
        const char *s = envp[i] ? envp[i] : "";
        size_t l = strlen(s) + 1;
        sp -= l;
        memcpy((void *)(uintptr_t)sp, s, l);
        env_ptrs[i] = sp;
    }
    sp &= ~0xFULL;                 /* lowest string address, 16-aligned */
    uint64_t str_lo = sp;

    /* Vectors (high -> low): [envp NULL][envp...][argv NULL][argv...][argc].
       Reserve it just below the strings and 16-byte align the entry RSP so that,
       at process entry, %rsp points at argc (16-aligned) with argv[0] at %rsp+8
       (so `call main` in crt0 yields RSP%16==8 inside main, per the SysV ABI). */
    uint64_t base = str_lo - 8UL * (argc + envc + 3);
    base &= ~0xFULL;              /* entry RSP, 16-aligned, points at argc */

    if (base < stack_base)
        return -1;                /* args too big; would underflow the stack */

    uint64_t w = base + 8UL * (argc + envc + 2);   /* envp NULL slot */
    *(uint64_t *)(uintptr_t)w = 0;
    for (int i = envc - 1; i >= 0; i--)
    {
        w -= 8;
        *(uint64_t *)(uintptr_t)w = env_ptrs[i];
    }
    w -= 8;
    *(uint64_t *)(uintptr_t)w = 0;          /* argv NULL terminator */
    for (int i = argc - 1; i >= 0; i--)
    {
        w -= 8;
        *(uint64_t *)(uintptr_t)w = str_ptrs[i];
    }
    /* w == base + 8 == argv[0] slot */
    *(uint64_t *)(uintptr_t)base = (uint64_t)argc;

    *stack_top = base;

    return 0;
}

int exec_user_program(const uint8_t *elf, size_t size, const char *name)
{
    struct mmu_root *mmu = vmm_new_user_root();
    if (!mmu)
    {
        printk("EXEC: no address space for '%s'\n", name);
        return -1;
    }
    char *av[1];
    av[0] = (char *)name;
    uint64_t entry, stack_top;
    if (elf_load(mmu, elf, size, &entry, &stack_top, 1, av, 0, 0) < 0)
    {
        printk("EXEC: failed to load ELF '%s'\n", name);
        vmm_free_root(mmu);
        return -1;
    }
    printk("EXEC: loaded '%s' entry=0x%lx stack=0x%lx\n",
            name, entry, stack_top);
    struct thread *t = sched_spawn_user_in(mmu, (void *)entry, (void *)stack_top,
                                           0x202, name, 0, 0, 0, 0, 0, 0);
    if (t)
        sched_mark_init(t);
    return 0;
}

int spawn_process_with_args(const uint8_t *elf, size_t size, const char *name,
                            int argc, char **argv)
{
    struct mmu_root *mmu = vmm_new_user_root();
    if (!mmu)
        return -1;
    uint64_t entry, stack_top;
    if (elf_load(mmu, elf, size, &entry, &stack_top, argc, argv, 0, 0) < 0)
    {
        vmm_free_root(mmu);
        return -1;
    }
    struct thread *t = sched_spawn_user_in(mmu, (void *)entry, (void *)stack_top, 0x202,
                                            name, 0, 0, 0, 0, 0, 0);
    return t ? (int)t->pid : -1;
}

int fork_process(uint64_t rip, uint64_t rsp, uint64_t rflags,
                 uint64_t rbx, uint64_t rbp, uint64_t r12,
                 uint64_t r13, uint64_t r14, uint64_t r15)
{
    struct thread *cur = sched_current();
    if (!cur)
        return -1;
    struct mmu_root *mmu = vmm_clone_root(cur->mmu);
    if (!mmu)
        return -1;
    struct thread *t = sched_spawn_user_in(mmu, (void *)(uintptr_t)rip,
                                            (void *)(uintptr_t)rsp, rflags, cur->name,
                                            rbx, rbp, r12, r13, r14, r15);
    if (t)
    {
        /* Inherit signal dispositions (pending cleared) and IPC channels. */
        for (int i = 0; i < NSIG; i++)
            t->sig_actions[i] = cur->sig_actions[i];
        t->sig_mask = cur->sig_mask;
        t->sig_pending = 0;
        for (int i = 0; i < IPC_MAX; i++)
            t->ipc[i] = cur->ipc[i];
    }
    return t ? (int)t->pid : -1;
}