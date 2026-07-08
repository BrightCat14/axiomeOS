#include "module.h"
#include "printk.h"
#include "slab.h"
#include "string.h"
#include "pmm.h"
#include "vmm.h"
#include "vfs.h"

/* ===========================================================================
 * .kxt loader
 *
 * Modules are ET_REL (relocatable) ELF64 objects.  We lay out every SHF_ALLOC
 * section into one contiguous, page-aligned kernel-virtual region, copy the
 * PROGBITS, zero the NOBITS, then apply the SHT_RELA relocations.  External
 * (undefined) symbols are resolved by name against the .ksymtab export table.
 * Because modules are built with -mcmodel=large, all inter-module references
 * are 64-bit absolute and need no proximity to the kernel text.
 * =========================================================================== */

#define MODULE_VA_BASE 0xFFFFFFFFFFA00000ULL   /* 512 MiB above kernel text */
#define MODULE_MAX     16

/* Linker-provided bounds of the exported-symbol table. */
extern struct ksym __ksymtab_start[];
extern struct ksym __ksymtab_end[];

/* ---- minimal ELF64 definitions (section/symbol view) ---- */
#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define ET_REL      1
#define EM_X86_64   62

#define SHT_PROGBITS 1
#define SHT_SYMTAB   2
#define SHT_STRTAB   3
#define SHT_RELA     4
#define SHT_NOBITS   8

#define SHF_ALLOC   0x2

#define SHN_UNDEF   0
#define SHN_ABS     0xFFF1

#define R_X86_64_64       1
#define R_X86_64_PC32     2
#define R_X86_64_PLT32    4
#define R_X86_64_RELATIVE 8

struct elf_shdr {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
};

struct elf_sym {
    uint32_t st_name;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
};

struct elf_rela {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
};

#define ELF_R_SYM(i)  ((uint32_t)((i) >> 32))
#define ELF_R_TYPE(i) ((uint32_t)(i))

/* ---- loaded-module bookkeeping ---- */
struct module {
    char     name[32];
    uint64_t base;       /* virtual base of the loaded image */
    uint64_t size;       /* bytes (multiple of PAGE_SIZE)     */
    uint64_t *pages;     /* physical page pointers            */
    uint64_t npages;
    uint64_t exit_addr;  /* module_exit, or 0 if none         */
    int      loaded;
};

static struct module g_mods[MODULE_MAX];
static int g_nmods;
static uint64_t g_mod_va_cursor = MODULE_VA_BASE;

static void *ksym_lookup(const char *name)
{
    for (struct ksym *k = __ksymtab_start; k < __ksymtab_end; k++)
        if (k->name && strcmp(k->name, name) == 0)
            return k->addr;
    return 0;
}

/* Find a defined symbol by name in `symtab` (count `nsyms`, strtab `str`). */
static const struct elf_sym *find_sym(const struct elf_sym *symtab, int nsyms,
                                      const char *str, const char *name)
{
    for (int i = 0; i < nsyms; i++)
    {
        if (symtab[i].st_shndx == SHN_UNDEF)
            continue;
        if (strcmp(str + symtab[i].st_name, name) == 0)
            return &symtab[i];
    }
    return 0;
}

static uint64_t align_up(uint64_t v, uint64_t a)
{
    if (a <= 1) return v;
    return (v + a - 1) & ~(a - 1);
}

int module_load(const uint8_t *elf, size_t size, const char *name)
{
    if (size < sizeof(struct elf_shdr) + 64)
        return -1;
    if (elf[0] != 0x7F || elf[1] != 'E' || elf[2] != 'L' || elf[3] != 'F')
        return -1;
    if (elf[4] != ELFCLASS64 || elf[5] != ELFDATA2LSB)
        return -1;

    struct elf_ehdr {
        uint8_t  e_ident[16];
        uint16_t e_type;
        uint16_t e_machine;
        uint32_t e_version;
        uint64_t e_entry;
        uint64_t e_phoff;
        uint64_t e_shoff;
        uint32_t e_flags;
        uint16_t e_ehsize;
        uint16_t e_phentsize;
        uint16_t e_phnum;
        uint16_t e_shentsize;
        uint16_t e_shnum;
        uint16_t e_shstrndx;
    } *eh = (void *)elf;

    if (eh->e_type != ET_REL || eh->e_machine != EM_X86_64)
    {
        printk("KXT: not a relocatable x86_64 ELF\n");
        return -1;
    }
    if (eh->e_shnum == 0 || eh->e_shoff == 0)
    {
        printk("KXT: no section headers\n");
        return -1;
    }

    const struct elf_shdr *shdrs = (const struct elf_shdr *)(elf + eh->e_shoff);
    const struct elf_shdr *shstr = &shdrs[eh->e_shstrndx];

    /* Locate the symbol table + its string table. */
    const struct elf_sym *symtab = 0;
    const char          *symstr = 0;
    int nsyms = 0;
    for (int i = 0; i < eh->e_shnum; i++)
    {
        if (shdrs[i].sh_type == SHT_SYMTAB)
        {
            symtab = (const struct elf_sym *)(elf + shdrs[i].sh_offset);
            nsyms  = (int)(shdrs[i].sh_size / sizeof(struct elf_sym));
            symstr = (const char *)(elf + shdrs[shdrs[i].sh_link].sh_offset);
            break;
        }
    }
    if (!symtab)
    {
        printk("KXT: no symbol table\n");
        return -1;
    }

    for (int i = 0; i < g_nmods; i++)
        if (g_mods[i].loaded && strcmp(g_mods[i].name, name) == 0)
        {
            printk("KXT: '%s' already loaded\n", name);
            return -1;
        }
    if (g_nmods >= MODULE_MAX)
    {
        printk("KXT: module table full\n");
        return -1;
    }

    /* ---- Pass 1: lay out SHF_ALLOC sections contiguously. ---- */
    uint64_t layout_off = 0;
    uint64_t *load_addr = (uint64_t *)kmalloc(sizeof(uint64_t) * eh->e_shnum);
    if (!load_addr) return -1;
    for (int i = 0; i < eh->e_shnum; i++)
        load_addr[i] = 0;

    for (int i = 0; i < eh->e_shnum; i++)
    {
        const struct elf_shdr *s = &shdrs[i];
        if (!(s->sh_flags & SHF_ALLOC) || s->sh_size == 0)
            continue;
        layout_off = align_up(layout_off, s->sh_addralign ? s->sh_addralign : 1);
        load_addr[i] = layout_off;
        layout_off += s->sh_size;
    }

    uint64_t total  = align_up(layout_off, PAGE_SIZE);
    uint64_t npages = total / PAGE_SIZE;
    uint64_t base   = g_mod_va_cursor;
    uint64_t *pages = (uint64_t *)kmalloc(sizeof(uint64_t) * npages);
    if (!pages) { kfree(load_addr); return -1; }

    for (uint64_t p = 0; p < npages; p++)
    {
        void *phys = pmm_alloc_frame();
        if (!phys)
        {
            printk("KXT: out of memory allocating module pages\n");
            for (uint64_t q = 0; q < p; q++)
            {
                vmm_unmap_page(base + q * PAGE_SIZE);
                pmm_free_frame((void *)(uintptr_t)pages[q]);
            }
            kfree(pages); kfree(load_addr);
            return -1;
        }
        pages[p] = (uint64_t)(uintptr_t)phys;
        if (vmm_map_page(base + p * PAGE_SIZE, (uint64_t)(uintptr_t)phys,
                         PTE_WRITE) < 0)
        {
            printk("KXT: vmm_map_page failed\n");
            for (uint64_t q = 0; q <= p; q++)
            {
                vmm_unmap_page(base + q * PAGE_SIZE);
                pmm_free_frame((void *)(uintptr_t)pages[q]);
            }
            kfree(pages); kfree(load_addr);
            return -1;
        }
    }

    /* ---- Pass 2: copy PROGBITS, zero NOBITS. ---- */
    for (int i = 0; i < eh->e_shnum; i++)
    {
        const struct elf_shdr *s = &shdrs[i];
        if (!(s->sh_flags & SHF_ALLOC) || s->sh_size == 0)
            continue;
        uint8_t *dst = (uint8_t *)(base + load_addr[i]);
        if (s->sh_type == SHT_NOBITS)
            memset(dst, 0, s->sh_size);
        else
            memcpy(dst, elf + s->sh_offset, s->sh_size);
    }

    /* ---- Pass 3: apply RELA relocations. ---- */
    int rc = 0;
    for (int i = 0; i < eh->e_shnum; i++)
    {
        const struct elf_shdr *s = &shdrs[i];
        if (s->sh_type != SHT_RELA)
            continue;
        const struct elf_rela *rel = (const struct elf_rela *)(elf + s->sh_offset);
        int nrel = (int)(s->sh_size / sizeof(struct elf_rela));
        int tgt = (int)s->sh_info;      /* section being relocated */
        uint64_t tgt_base = load_addr[tgt];
        if (!(shdrs[tgt].sh_flags & SHF_ALLOC))
            continue;                   /* not an allocated section */

        for (int r = 0; r < nrel; r++)
        {
            uint32_t symidx = ELF_R_SYM(rel[r].r_info);
            uint32_t type   = ELF_R_TYPE(rel[r].r_info);
            const struct elf_sym *sym = &symtab[symidx];

            uint64_t S;
            if (sym->st_shndx == SHN_UNDEF)
            {
                const char *nm = symstr + sym->st_name;
                void *addr = ksym_lookup(nm);
                if (!addr)
                {
                    printk("KXT: unresolved symbol '%s' in module '%s'\n",
                           nm, name);
                    rc = -1;
                    break;
                }
                S = (uint64_t)(uintptr_t)addr;
            }
            else if (sym->st_shndx == SHN_ABS)
            {
                S = sym->st_value;
            }
            else
            {
                S = base + load_addr[sym->st_shndx] + sym->st_value;
            }

            uint64_t P = base + tgt_base + rel[r].r_offset;
            uint64_t A = (uint64_t)rel[r].r_addend;

            switch (type)
            {
            case R_X86_64_64:
                *(uint64_t *)P = S + A;
                break;
            case R_X86_64_PC32:
            case R_X86_64_PLT32:
                *(uint32_t *)P = (uint32_t)(S + A - P);
                break;
            case R_X86_64_RELATIVE:
                *(uint64_t *)P = base + A;
                break;
            case 0: /* R_X86_64_NONE */
                break;
            default:
                printk("KXT: unsupported relocation type %u in '%s'\n",
                       type, name);
                rc = -1;
                break;
            }
            if (rc) break;
        }
        if (rc) break;
    }

    if (rc)
    {
        for (uint64_t p = 0; p < npages; p++)
        {
            vmm_unmap_page(base + p * PAGE_SIZE);
            pmm_free_frame((void *)(uintptr_t)pages[p]);
        }
        kfree(pages); kfree(load_addr);
        return -1;
    }

    /* ---- Locate entry points (needs load_addr, so before freeing). ---- */
    const struct elf_sym *init_sym = find_sym(symtab, nsyms, symstr, "module_init");
    const struct elf_sym *exit_sym = find_sym(symtab, nsyms, symstr, "module_exit");

    if (!init_sym)
    {
        printk("KXT: '%s' has no module_init\n", name);
        for (uint64_t p = 0; p < npages; p++)
        {
            vmm_unmap_page(base + p * PAGE_SIZE);
            pmm_free_frame((void *)(uintptr_t)pages[p]);
        }
        kfree(pages); kfree(load_addr);
        return -1;
    }

    uint64_t init_addr = base + load_addr[init_sym->st_shndx] + init_sym->st_value;
    uint64_t exit_addr  = exit_sym
        ? (base + load_addr[exit_sym->st_shndx] + exit_sym->st_value) : 0;

    struct module *m = &g_mods[g_nmods++];
    strncpy(m->name, name, 31); m->name[31] = 0;
    m->base   = base;
    m->size   = total;
    m->pages  = pages;
    m->npages = npages;
    m->exit_addr = exit_addr;
    m->loaded = 1;

    g_mod_va_cursor += total;
    (void)shstr;
    kfree(load_addr);

    printk("KXT: loaded '%s' at %p (%lu pages)\n", name, (void *)base, npages);
    void (*init_fn)(void) = (void (*)(void))init_addr;
    if (init_fn)
        init_fn();

    return 0;
}

int module_load_file(const char *path)
{
    uint8_t *buf = 0;
    size_t sz = 0;
    if (vfs_read_file(path, &buf, &sz) != 0)
    {
        printk("KXT: cannot read '%s'\n", path);
        return -1;
    }

    /* Derive a module name from the file's basename (strip .kxt). */
    const char *slash = path;
    for (const char *p = path; *p; p++)
        if (*p == '/') slash = p + 1;
    char name[32];
    int i = 0;
    for (const char *p = slash; *p && i < 31; p++)
    {
        if (*p == '.') break;
        name[i++] = *p;
    }
    name[i] = 0;

    int rc = module_load(buf, sz, name);
    kfree(buf);
    return rc;
}

int module_unload(const char *name)
{
    for (int i = 0; i < g_nmods; i++)
    {
        struct module *m = &g_mods[i];
        if (m->loaded && strcmp(m->name, name) == 0)
        {
            if (m->exit_addr)
            {
                void (*exit_fn)(void) = (void (*)(void))m->exit_addr;
                exit_fn();
            }
            for (uint64_t p = 0; p < m->npages; p++)
            {
                vmm_unmap_page(m->base + p * PAGE_SIZE);
                pmm_free_frame((void *)(uintptr_t)m->pages[p]);
            }
            kfree(m->pages);
            m->loaded = 0;
            printk("KXT: unloaded '%s'\n", name);
            return 0;
        }
    }
    printk("KXT: '%s' not loaded\n", name);
    return -1;
}

void module_list(void)
{
    printk("KXT: %d module(s) loaded:\n", g_nmods);
    for (int i = 0; i < g_nmods; i++)
        if (g_mods[i].loaded)
            printk("  %s @ %p (%lu KB)\n", g_mods[i].name,
                   (void *)g_mods[i].base, g_mods[i].size / 1024);
}

void module_init_subsys(void)
{
    printk("KXT: subsystem ready (base %p)\n", (void *)MODULE_VA_BASE);
}
