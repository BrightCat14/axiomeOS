/* Minimal ELF64 PT_LOAD loader: place segments at p_paddr. */

#include "bootloader.h"

#define EI_MAG0 0
#define EI_MAG1 1
#define EI_MAG2 2
#define EI_MAG3 3
#define EI_CLASS 4
#define EI_DATA 5
#define ELFMAG0 0x7F
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'
#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define ET_EXEC 2
#define EM_X86_64 62
#define PT_LOAD 1

struct elf64_ehdr {
    UINT8  ident[16];
    UINT16 type;
    UINT16 machine;
    UINT32 version;
    UINT64 entry;
    UINT64 phoff;
    UINT64 shoff;
    UINT32 flags;
    UINT16 ehsize;
    UINT16 phentsize;
    UINT16 phnum;
    UINT16 shentsize;
    UINT16 shnum;
    UINT16 shstrndx;
};

struct elf64_phdr {
    UINT32 type;
    UINT32 flags;
    UINT64 offset;
    UINT64 vaddr;
    UINT64 paddr;
    UINT64 filesz;
    UINT64 memsz;
    UINT64 align;
};

static void mem_copy(UINT8 *dst, const UINT8 *src, UINTN n)
{
    for (UINTN i = 0; i < n; i++)
        dst[i] = src[i];
}

static void mem_zero(UINT8 *dst, UINTN n)
{
    for (UINTN i = 0; i < n; i++)
        dst[i] = 0;
}

EFI_STATUS
elf_load(struct boot_ctx *ctx)
{
    struct elf64_ehdr *eh;
    struct elf64_phdr *ph;
    UINT64 lo = (UINT64)-1, hi = 0;
    int loads = 0;

    if (ctx->file_size < sizeof(*eh))
        return EFI_LOAD_ERROR;
    eh = (struct elf64_ehdr *)ctx->file_data;

    if (eh->ident[EI_MAG0] != ELFMAG0 || eh->ident[EI_MAG1] != ELFMAG1 ||
        eh->ident[EI_MAG2] != ELFMAG2 || eh->ident[EI_MAG3] != ELFMAG3)
    {
        bl_print("axboot: bad ELF magic\n");
        return EFI_LOAD_ERROR;
    }
    if (eh->ident[EI_CLASS] != ELFCLASS64 || eh->ident[EI_DATA] != ELFDATA2LSB)
    {
        bl_print("axboot: not 64-bit LE ELF\n");
        return EFI_LOAD_ERROR;
    }
    if (eh->machine != EM_X86_64)
    {
        bl_print("axboot: not x86_64 ELF\n");
        return EFI_LOAD_ERROR;
    }
    if (eh->phentsize != sizeof(*ph))
    {
        bl_print("axboot: bad phentsize\n");
        return EFI_LOAD_ERROR;
    }
    if (eh->phoff + (UINT64)eh->phnum * sizeof(*ph) > ctx->file_size)
    {
        bl_print("axboot: phdrs past EOF\n");
        return EFI_LOAD_ERROR;
    }

    /* First pass: geometry. */
    for (UINT16 i = 0; i < eh->phnum; i++)
    {
        ph = (struct elf64_phdr *)(ctx->file_data + eh->phoff +
                                   (UINT64)i * sizeof(*ph));
        if (ph->type != PT_LOAD || ph->memsz == 0)
            continue;
        if (ph->paddr < lo)
            lo = ph->paddr;
        if (ph->paddr + ph->memsz > hi)
            hi = ph->paddr + ph->memsz;
        loads++;
    }
    if (!loads)
    {
        bl_print("axboot: no PT_LOAD segments\n");
        return EFI_LOAD_ERROR;
    }

    /* Allocate the whole image span once: PT_LOAD segments share pages
       (the linker packs .text/.rodata end-to-start), so per-segment
       AllocateAddress would collide on the shared page. */
    {
        EFI_PHYSICAL_ADDRESS at;
        EFI_STATUS st;
        UINTN pages;
        UINT64 lo_page = lo & ~0xFFFULL;
        UINT64 hi_page = (hi + 0xFFFULL) & ~0xFFFULL;

        pages = (UINTN)((hi_page - lo_page) >> 12);
        at = (EFI_PHYSICAL_ADDRESS)lo_page;
        st = ctx->bs->AllocatePages(AllocateAddress, EfiLoaderData,
                                    pages, &at);
        if (EFI_ERROR(st))
        {
            bl_print("axboot: AllocatePages failed for kernel span ");
            bl_print_hex(lo_page);
            bl_print("\n");
            return st;
        }
        mem_zero((UINT8 *)(UINTN)lo_page, (UINTN)(hi_page - lo_page));
    }

    /* Second pass: copy each segment's file bytes into place (bss tails
       are already zero from the span wipe). */
    for (UINT16 i = 0; i < eh->phnum; i++)
    {
        UINT8 *dst;

        ph = (struct elf64_phdr *)(ctx->file_data + eh->phoff +
                                   (UINT64)i * sizeof(*ph));
        if (ph->type != PT_LOAD || ph->memsz == 0)
            continue;
        if (ph->offset + ph->filesz > ctx->file_size)
        {
            bl_print("axboot: segment past EOF\n");
            return EFI_LOAD_ERROR;
        }
        dst = (UINT8 *)(UINTN)ph->paddr;
        mem_copy(dst, ctx->file_data + ph->offset, (UINTN)ph->filesz);
    }

    ctx->kern_entry = eh->entry;
    ctx->kern_phys = lo;
    ctx->kern_size = hi - lo;
    return EFI_SUCCESS;
}
