/* Loader page tables: identity-map low RAM (2 MiB huge pages) plus the
   kernel's higher-half alias, then jump to the 64-bit kernel entry with
   RDI = physical address of struct axboot_info. */

#include "bootloader.h"

#define PTE_PRESENT (1ULL << 0)
#define PTE_WRITE (1ULL << 1)
#define PTE_HUGE (1ULL << 7)

EFI_STATUS
boot_tables_build(struct boot_ctx *ctx)
{
    UINT64 *pml4;
    UINT64 *pdpt_lo;
    UINT64 *pdpt_hi;
    UINT64 *pd_kern;
    UINT64 tables = ctx->tables_phys;
    UINT64 n_pd;
    UINT64 kern_phys_al;
    UINT64 kern_virt = AXBOOT_KERNEL_VIRT_BASE;
    UINT64 kern_pages;
    UINT64 i;

    if (ctx->tables_pages < 5)
        return EFI_DEVICE_ERROR;
    /* Layout needs PML4 + PDPT_lo + n_pd PDs + PDPT_hi + PD_kern. */
    n_pd = ctx->tables_pages - 4;

    /* Layout inside the allocated run:
         [0]        PML4
         [1]        PDPT_lo (identity)
         [2..2+n_pd) PDs for identity (each 1 GiB)
         [2+n_pd]   PDPT_hi
         [3+n_pd]   PD_kern */
    pml4 = (UINT64 *)(UINTN)(tables + 0 * 4096);
    pdpt_lo = (UINT64 *)(UINTN)(tables + 1 * 4096);
    pdpt_hi = (UINT64 *)(UINTN)(tables + (2 + n_pd) * 4096);
    pd_kern = (UINT64 *)(UINTN)(tables + (3 + n_pd) * 4096);

    for (i = 0; i < 512; i++)
    {
        pml4[i] = 0;
        pdpt_lo[i] = 0;
        pdpt_hi[i] = 0;
        pd_kern[i] = 0;
    }
    for (UINT64 p = 0; p < n_pd; p++)
    {
        UINT64 *pd = (UINT64 *)(UINTN)(tables + (2 + p) * 4096);
        for (i = 0; i < 512; i++)
            pd[i] = 0;
    }

    /* Identity: PD[p][i] = (p*1GiB + i*2MiB) | P|W|PS. */
    for (UINT64 p = 0; p < n_pd; p++)
    {
        UINT64 *pd = (UINT64 *)(UINTN)(tables + (2 + p) * 4096);
        for (i = 0; i < 512; i++)
            pd[i] = (p * 0x40000000ULL + i * 0x200000ULL) | PTE_PRESENT |
                PTE_WRITE | PTE_HUGE;
        pdpt_lo[p] = (tables + (2 + p) * 4096ULL) | PTE_PRESENT | PTE_WRITE;
    }
    pml4[0] = (UINT64)(UINTN)pdpt_lo | PTE_PRESENT | PTE_WRITE;

    /* Higher-half kernel alias: phys P maps to virt P + BASE (the linker
       sets p_paddr = p_vaddr - BASE, so V = P + BASE). The kernel image
       starts at virt BASE+0x200000, i.e. PD index 1, not 0. */
    kern_phys_al = ctx->kern_phys & ~0x1FFFFFULL;
    kern_pages = (ctx->kern_phys - kern_phys_al + ctx->kern_size +
                  0x1FFFFFULL) >> 21;
    for (i = 0; i < kern_pages; i++)
    {
        UINT64 p = kern_phys_al + i * 0x200000ULL;
        UINT64 idx = (p >> 21) & 0x1FF;
        if (idx >= 512)
            break;
        /* P < 1 GiB keeps us inside the PDP[510] window (which spans
           0xFFFFFFFF80000000-0xFFFFFFFFBFFFFFFF). */
        if (p >= 0x40000000ULL)
            break;
        pd_kern[idx] = p | PTE_PRESENT | PTE_WRITE | PTE_HUGE;
    }
    /* PML4[511], PDPT[510] select 0xFFFFFFFF80000000. */
    pdpt_hi[510] = (UINT64)(UINTN)pd_kern | PTE_PRESENT | PTE_WRITE;
    pml4[511] = (UINT64)(UINTN)pdpt_hi | PTE_PRESENT | PTE_WRITE;

    (void)kern_virt;
    return EFI_SUCCESS;
}

/* Args arrive in System V registers; bind them explicitly so the
   compiler cannot reorder them under us. RDI is clobbered (it receives
   info_phys), so entry is bounced through RAX first. */
void
boot_handoff(UINT64 entry, UINT64 info_phys, UINT64 stack_top, UINT64 cr3)
{
    __asm__ volatile(
        "cli\n"
        "mov %0, %%rax\n" /* stash entry before clobbering RDI */
        "mov %3, %%cr3\n" /* new tables first (old stack still mapped) */
        "mov %2, %%rsp\n" /* handoff stack (identity-mapped) */
        "mov %1, %%rdi\n" /* RDI = bootinfo phys */
        "jmp *%%rax\n"
        :
        : "r"(entry), "r"(info_phys), "r"(stack_top), "r"(cr3)
        : "rax", "rdi", "memory");
    for (;;)
        __asm__ volatile("hlt");
}
