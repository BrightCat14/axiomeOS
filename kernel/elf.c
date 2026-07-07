#include "elf.h"
#include "printk.h"
#include "vmm.h"
#include "pmm.h"
#include "sched.h"
#include "string.h"

static int setup_user_stack(uint64_t stack_base, uint64_t stack_pages,
                            int argc, char **argv, uint64_t *stack_top);

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

int elf_load(uint64_t *pml4, const uint8_t *data, size_t size,
              uint64_t *entry, uint64_t *stack_top, int argc, char **argv)
{
    if (size < sizeof(struct elf64_ehdr))
        return -1;

    const struct elf64_ehdr *h = (const struct elf64_ehdr *)data;
    if (!elf_valid(h))
    {
        printk("ELF: invalid header\n");
        return -1;
    }

    /* The new pml4 owns these user mappings, but it is not yet the active
       page table. Switch CR3 so writes to the user virtual addresses land
       in the correct frames. */
    uint64_t saved_cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(saved_cr3));
    vmm_switch(pml4);

    uint64_t highest = 0;
    const struct elf64_phdr *ph = (const struct elf64_phdr *)(data + h->e_phoff);

    for (uint16_t i = 0; i < h->e_phnum; i++)
    {
        if (ph[i].p_type != PT_LOAD)
            continue;

        uint64_t vaddr  = ph[i].p_vaddr;
        uint64_t filesz = ph[i].p_filesz;
        uint64_t memsz  = ph[i].p_memsz;
        uint64_t offset = ph[i].p_offset;

        uint64_t flags = PTE_USER | PTE_WRITE;
        if (!(ph[i].p_flags & PF_X))
            flags |= PTE_NX;

        uint64_t vstart = vaddr & ~0xFFFULL;
        uint64_t vend   = (vaddr + memsz + 0xFFFULL) & ~0xFFFULL;
        uint64_t pages  = (vend - vstart) / PAGE_SIZE;

        for (uint64_t p = 0; p < pages; p++)
        {
            uint64_t va   = vstart + p * PAGE_SIZE;
            uint64_t phys = (uint64_t)pmm_alloc_frame();
            if (!phys)
            {
                printk("ELF: OOM mapping 0x%lx\n", va);
                return -1;
            }
            if (vmm_map_page_in(pml4, va, phys, flags) < 0)
            {
                pmm_free_frame((void *)phys);
                printk("ELF: map failed 0x%lx\n", va);
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
                __builtin_memcpy((void *)va, data + file_off, copy_len);
                if (copy_len < PAGE_SIZE)
                    __builtin_memset((void *)(va + copy_len), 0,
                                     PAGE_SIZE - copy_len);
            }
            else
            {
                __builtin_memset((void *)va, 0, PAGE_SIZE);
            }
        }

        uint64_t seg_end = vaddr + memsz;
        if (seg_end > highest)
            highest = seg_end;
    }

    *entry = h->e_entry;

    uint64_t stack_top_ptr = 0;
    uint64_t stack_base = (highest + 0x10000ULL + 0xFFFULL) & ~0xFFFULL;
    uint64_t stack_pages = 32;

    for (uint64_t p = 0; p < stack_pages; p++)
    {
        uint64_t va   = stack_base + p * PAGE_SIZE;
        uint64_t phys = (uint64_t)pmm_alloc_frame();
        if (!phys)
        {
            printk("ELF: OOM stack\n");
            return -1;
        }
        if (vmm_map_page_in(pml4, va, phys, PTE_USER | PTE_WRITE) < 0)
        {
            pmm_free_frame((void *)phys);
            return -1;
        }
    }

    if (setup_user_stack(stack_base, stack_pages, argc, argv, &stack_top_ptr))
        return -1;

    *stack_top = stack_top_ptr;

    vmm_switch((uint64_t *)(saved_cr3 & ~0xFFFULL));
    return 0;
}

/* Build an initial user stack: argc, argv[], NULL terminator, envp=NULL,
   with the argument strings copied just above the vector. Returns 0 on ok. */
static int setup_user_stack(uint64_t stack_base, uint64_t stack_pages,
                            int argc, char **argv, uint64_t *stack_top)
{
    uint64_t top = stack_base + stack_pages * PAGE_SIZE;
    uint64_t sp = top & ~0xFULL;

    if (argc < 0) argc = 0;
    if (argc > 32) argc = 32;

    uint64_t str_ptrs[32];
    for (int i = 0; i < argc; i++)
    {
        size_t l = strlen(argv[i]) + 1;
        sp -= l;
        memcpy((void *)(uintptr_t)sp, argv[i], l);
        str_ptrs[i] = sp;
    }
    sp &= ~0xFULL;                 /* lowest string address, 16-aligned */
    uint64_t str_lo = sp;

    /* Aux vector (high -> low): [envp=NULL][argv NULL][argv[argc-1]..argv[0]][argc].
       Reserve it just below the strings and 16-byte align the entry RSP so that,
       at process entry, %rsp points at argc (16-aligned) with argv[0] at %rsp+8
       (so `call main` in crt0 yields RSP%16==8 inside main, per the SysV ABI). */
    uint64_t base = str_lo - 8UL * (argc + 3);
    base &= ~0xFULL;              /* entry RSP, 16-aligned, points at argc */

    uint64_t w = base + 8UL * (argc + 2);   /* envp NULL slot */
    *(uint64_t *)(uintptr_t)w = 0;
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
    uint64_t *pml4 = vmm_new_user_pml4();
    if (!pml4)
    {
        printk("EXEC: no pml4 for '%s'\n", name);
        return -1;
    }
    char *av[1];
    av[0] = (char *)name;
    uint64_t entry, stack_top;
    if (elf_load(pml4, elf, size, &entry, &stack_top, 1, av) < 0)
    {
        printk("EXEC: failed to load ELF '%s'\n", name);
        vmm_free_pml4(pml4);
        return -1;
    }
    printk("EXEC: loaded '%s' entry=0x%lx stack=0x%lx\n",
            name, entry, stack_top);
    sched_spawn_user_in(pml4, (void *)entry, (void *)stack_top, 0x202, name,
                           0, 0, 0, 0, 0, 0);
    return 0;
}

int spawn_process(const uint8_t *elf, size_t size, const char *name)
{
    char *av[1];
    av[0] = (char *)name;
    return spawn_process_with_args(elf, size, name, 1, av);
}

int spawn_process_with_args(const uint8_t *elf, size_t size, const char *name,
                            int argc, char **argv)
{
    uint64_t *pml4 = vmm_new_user_pml4();
    if (!pml4)
        return -1;
    uint64_t entry, stack_top;
    if (elf_load(pml4, elf, size, &entry, &stack_top, argc, argv) < 0)
    {
        vmm_free_pml4(pml4);
        return -1;
    }
    struct thread *t = sched_spawn_user_in(pml4, (void *)entry, (void *)stack_top, 0x202,
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
    uint64_t *pml4 = vmm_clone_pml4(cur->pml4);
    if (!pml4)
        return -1;
    struct thread *t = sched_spawn_user_in(pml4, (void *)(uintptr_t)rip,
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
