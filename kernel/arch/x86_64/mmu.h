#ifndef AXIOME_X86_64_MMU_H
#define AXIOME_X86_64_MMU_H

/* Internal x86_64 page-table definitions. Only arch-specific code (mmu.c,
   hal/x86_mmio.cpp) may use these; portable code uses the MMU_* flags from
   arch_mmu.h. */

#define X86_PTE_PRESENT (1UL << 0)
#define X86_PTE_WRITE   (1UL << 1)
#define X86_PTE_USER    (1UL << 2)
#define X86_PTE_PWT     (1UL << 3)
#define X86_PTE_PCD     (1UL << 4)
#define X86_PTE_ACCESSED (1UL << 5)
#define X86_PTE_DIRTY   (1UL << 6)
#define X86_PTE_HUGE    (1UL << 7)
#define X86_PTE_GLOBAL  (1UL << 8)
#define X86_PTE_NX      (1UL << 63)

#endif
