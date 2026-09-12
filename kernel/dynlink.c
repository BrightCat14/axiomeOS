#include "dynlink.h"
#include "elf.h"

static void *dl_memset(void *dst, int c, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    while (n--)
        *d++ = (unsigned char)c;
    return dst;
}

static int dl_strcmp(const uint8_t *a, const uint8_t *b)
{
    while (*a && *a == *b)
    {
        a++;
        b++;
    }
    return (int)*a - (int)*b;
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int buf_ok(const uint8_t *buf, size_t size, uint64_t off, uint64_t len)
{
    (void)buf;
    return off <= size && len <= size - off;
}

/* Translate a virtual address from a program header into a file offset. */
static int va_to_off(const struct elf64_phdr *ph, size_t nph,
                     uint64_t va, uint64_t *off)
{
    for (size_t i = 0; i < nph; i++)
    {
        if (ph[i].p_type != PT_LOAD)
            continue;
        if (va >= ph[i].p_vaddr && va < ph[i].p_vaddr + ph[i].p_filesz)
        {
            *off = ph[i].p_offset + (va - ph[i].p_vaddr);
            return 0;
        }
    }
    return -1;
}

/* .dynsym count from a SHT_DYNSYM section header, if sections survive in
   the file (they do for unstripped axiome images). Fallback only. */
static size_t dynsym_count_from_sections(const uint8_t *buf, size_t size,
                                         uint64_t symtab_va)
{
    if (size < sizeof(struct elf64_ehdr))
        return 0;
    const struct elf64_ehdr *h = (const struct elf64_ehdr *)buf;
    if (h->e_shentsize != sizeof(struct elf64_shdr) || h->e_shoff == 0)
        return 0;
    if (h->e_shoff > size)
        return 0;
    size_t nsh = h->e_shnum;
    if (nsh > (size - h->e_shoff) / sizeof(struct elf64_shdr))
        nsh = (size - h->e_shoff) / sizeof(struct elf64_shdr);
    const struct elf64_shdr *sh =
        (const struct elf64_shdr *)(buf + h->e_shoff);
    for (size_t i = 0; i < nsh; i++)
    {
        if (sh[i].sh_type != SHT_DYNSYM || sh[i].sh_addr != symtab_va)
            continue;
        size_t entsz = sh[i].sh_entsize ? (size_t)sh[i].sh_entsize : 24;
        if (entsz)
            return (size_t)(sh[i].sh_size / entsz);
    }
    return 0;
}

int elf_parse_dynamic(struct elf_object *obj)
{
    const uint8_t *buf = obj->buf;
    size_t size = obj->size;

    dl_memset(&obj->info, 0, sizeof(obj->info));

    if (size < sizeof(struct elf64_ehdr))
        return -1;
    const struct elf64_ehdr *h = (const struct elf64_ehdr *)buf;
    if (h->ei_magic != ELF_MAGIC)
        return -1;
    if (h->e_phentsize != sizeof(struct elf64_phdr) || h->e_phnum == 0)
        return -1;
    if (h->e_phoff > size ||
        (uint64_t)h->e_phnum > (size - h->e_phoff) / sizeof(struct elf64_phdr))
        return -1;

    obj->info.entry = h->e_entry;

    const struct elf64_phdr *ph =
        (const struct elf64_phdr *)(buf + h->e_phoff);
    size_t nph = h->e_phnum;
    const struct elf64_phdr *dynph = 0;

    /* Bounds-check every program header and gather the loaded span so the
       relocation pass can reject out-of-range writes. */
    for (size_t i = 0; i < nph; i++)
    {
        if (ph[i].p_type != PT_LOAD)
        {
            if (ph[i].p_type == PT_DYNAMIC)
                dynph = &ph[i];
            continue;
        }
        if (ph[i].p_offset > size ||
            ph[i].p_filesz > size - ph[i].p_offset)
            return -1;
        if (ph[i].p_filesz > ph[i].p_memsz)
            return -1;
        if (ph[i].p_vaddr + ph[i].p_filesz < ph[i].p_vaddr)
            return -1;
        if (ph[i].p_vaddr < obj->info.seg_lo ||
            obj->info.seg_lo == 0)
            obj->info.seg_lo = ph[i].p_vaddr;
        if (ph[i].p_vaddr + ph[i].p_memsz > obj->info.seg_hi)
            obj->info.seg_hi = ph[i].p_vaddr + ph[i].p_memsz;
    }
    if (!dynph)
        return 0;                       /* static image: nothing to parse */
    obj->info.has_dynamic = 1;

    uint64_t d_off;
    if (va_to_off(ph, nph, dynph->p_vaddr, &d_off) < 0)
        return -1;
    if (d_off >= size)
        return -1;
    size_t d_cnt = (size_t)(dynph->p_filesz / sizeof(struct elf64_dyn));
    if (d_off + d_cnt * sizeof(struct elf64_dyn) > size)
        d_cnt = (size - d_off) / sizeof(struct elf64_dyn);
    obj->info.dyn_off = d_off;
    obj->info.dyn_count = d_cnt;
    const struct elf64_dyn *dyn =
        (const struct elf64_dyn *)(buf + d_off);

    uint64_t strt = 0, strsz = 0, symt = 0, rela = 0, relasz = 0,
             relaent = 0, jmp = 0, pltrsz = 0, hash = 0;
    for (size_t i = 0; i < d_cnt; i++)
    {
        switch (dyn[i].d_tag)
        {
        case DT_NEEDED:
            if (obj->info.nneeded < DYNLINK_MAX_DEPS)
                obj->info.needed_off[obj->info.nneeded++] =
                    (uint32_t)dyn[i].d_un.d_val;
            break;
        case DT_STRTAB: strt = dyn[i].d_un.d_ptr; break;
        case DT_STRSZ:  strsz = dyn[i].d_un.d_val; break;
        case DT_SYMTAB: symt  = dyn[i].d_un.d_ptr; break;
        case DT_RELA:   rela  = dyn[i].d_un.d_ptr; break;
        case DT_RELASZ: relasz = dyn[i].d_un.d_val; break;
        case DT_RELAENT: relaent = dyn[i].d_un.d_val; break;
        case DT_JMPREL: jmp   = dyn[i].d_un.d_ptr; break;
        case DT_PLTRELSZ: pltrsz = dyn[i].d_un.d_val; break;
        case DT_HASH:   hash  = dyn[i].d_un.d_ptr; break;
        case DT_NULL:
            i = d_cnt;
            break;
        default:
            break;
        }
    }

    if (strt)
    {
        uint64_t off;
        if (va_to_off(ph, nph, strt, &off) == 0 && off < size)
        {
            obj->info.dynstr_off = off;
            size_t cap = strsz < size - off ? (size_t)strsz : (size_t)(size - off);
            obj->info.dynstr_size = cap;
        }
    }

    if (symt)
    {
        uint64_t off;
        if (va_to_off(ph, nph, symt, &off) == 0 && off < size)
        {
            obj->info.dynsym_off = off;
            size_t max_syms = (size - off) / sizeof(struct elf64_sym);
            size_t count = 0;
            if (hash)
            {
                uint64_t hoff;
                if (va_to_off(ph, nph, hash, &hoff) == 0 &&
                    buf_ok(buf, size, hoff, 8))
                    count = rd32(buf + hoff + 4);       /* nchain */
            }
            if (count == 0)
                count = dynsym_count_from_sections(buf, size, symt);
            if (count > max_syms)
                count = max_syms;
            obj->info.dynsym_count = count;
        }
    }

    if (rela && relaent)
    {
        uint64_t off;
        if (va_to_off(ph, nph, rela, &off) == 0 && off < size)
        {
            size_t n = relaent ? (size_t)(relasz / relaent) : 0;
            obj->info.rela_off = off;
            if (n > (size - off) / sizeof(struct elf64_rela))
                n = (size - off) / sizeof(struct elf64_rela);
            obj->info.rela_count = n;
        }
    }

    if (jmp)
    {
        uint64_t off;
        if (va_to_off(ph, nph, jmp, &off) == 0 && off < size)
        {
            size_t n = (size_t)(pltrsz / sizeof(struct elf64_rela));
            obj->info.jmprel_off = off;
            if (n > (size - off) / sizeof(struct elf64_rela))
                n = (size - off) / sizeof(struct elf64_rela);
            obj->info.jmprel_count = n;
        }
    }
    return 0;
}

uint64_t elf_resolve_symbol(struct elf_object *scope, size_t nscope,
                            const uint8_t *name, int *found)
{
    if (found)
        *found = 0;
    if (!name)
        return 0;

    for (size_t o = 0; o < nscope; o++)
    {
        const struct elf_dyninfo *di = &scope[o].info;
        if (!di->dynsym_off || !di->dynstr_off || di->dynsym_count == 0)
            continue;
        const uint8_t *dsym = scope[o].buf + di->dynsym_off;
        const uint8_t *dstr = scope[o].buf + di->dynstr_off;
        for (size_t i = 1; i < di->dynsym_count; i++)
        {
            const struct elf64_sym *s =
                (const struct elf64_sym *)(dsym + i * sizeof(struct elf64_sym));
            if (s->st_shndx == SHN_UNDEF)
                continue;
            if ((uint64_t)s->st_name >= di->dynstr_size)
                continue;
            if (dl_strcmp(dstr + s->st_name, name) == 0)
            {
                if (found)
                    *found = 1;
                return di->base + s->st_value;
            }
        }
    }
    return 0;
}

static int apply_one(struct elf_object *obj, const struct elf64_rela *rel,
                     struct elf_object *scope, size_t nscope,
                     dyn_place_fn place, void *ctx)
{
    const struct elf_dyninfo *di = &obj->info;
    uint64_t r_off = rel->r_offset;
    uint64_t target = di->base + r_off;
    if (target < di->seg_lo || target >= di->seg_hi)
        return -1;                              /* relocation outside image */

    uint32_t type = ELF64_R_TYPE(rel->r_info);
    uint32_t symi = ELF64_R_SYM(rel->r_info);

    if (symi == 0 && type != R_X86_64_64)
    {
        /* No symbol: behaves like R_X86_64_RELATIVE. */
        place(target, di->base + (uint64_t)rel->r_addend, ctx);
        return 0;
    }

    switch (type)
    {
    case R_X86_64_RELATIVE:
        place(target, di->base + (uint64_t)rel->r_addend, ctx);
        return 0;

    case R_X86_64_GLOB_DAT:
    case R_X86_64_JUMP_SLOT:
    case R_X86_64_64:
    {
        if (symi >= di->dynsym_count || !di->dynsym_off || !di->dynstr_off)
            return -1;
        const struct elf64_sym *s =
            (const struct elf64_sym *)(obj->buf + di->dynsym_off +
                                       (uint64_t)symi * sizeof(struct elf64_sym));
        if ((uint64_t)s->st_name >= di->dynstr_size)
            return -1;
        const uint8_t *name = obj->buf + di->dynstr_off + s->st_name;
        int found = 0;
        uint64_t val = elf_resolve_symbol(scope, nscope, name, &found);
        if (!found)
            return -1;
        if (type == R_X86_64_64)
            val += (uint64_t)rel->r_addend;
        place(target, val, ctx);
        return 0;
    }

    default:
        return -1;                              /* unsupported reloc type */
    }
}

int elf_apply_relocations(struct elf_object *obj, struct elf_object *scope,
                          size_t nscope, dyn_place_fn place, void *ctx,
                          int *applied)
{
    if (applied)
        *applied = 0;
    if (!obj->info.has_dynamic)
        return 0;

    const struct elf_dyninfo *di = &obj->info;

    if (di->rela_off && di->rela_count)
    {
        const struct elf64_rela *tab =
            (const struct elf64_rela *)(obj->buf + di->rela_off);
        for (size_t i = 0; i < di->rela_count; i++)
        {
            if (ELF64_R_TYPE(tab[i].r_info) == 0)
                continue;                       /* R_X86_64_NONE */
            if (apply_one(obj, &tab[i], scope, nscope, place, ctx) < 0)
                return -1;
            if (applied)
                (*applied)++;
        }
    }

    if (di->jmprel_off && di->jmprel_count)
    {
        const struct elf64_rela *tab =
            (const struct elf64_rela *)(obj->buf + di->jmprel_off);
        for (size_t i = 0; i < di->jmprel_count; i++)
        {
            if (ELF64_R_TYPE(tab[i].r_info) == 0)
                continue;
            if (apply_one(obj, &tab[i], scope, nscope, place, ctx) < 0)
                return -1;
            if (applied)
                (*applied)++;
        }
    }
    return 0;
}