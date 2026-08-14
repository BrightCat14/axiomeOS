/* x86_64 MMU: 4-level paging (PML4/PDP/PD/PT). Implements the portable
   arch_mmu.h interface. The root table is identity-mapped, so physical
   addresses of page tables double as kernel virtual addresses. */

#include <stddef.h>

#include "arch_mmu.h"
#include "mmu.h"
#include "pmm.h"
#include "mmap.h"

extern uint64_t mmap_max_addr;
extern uint64_t pd_table[];
extern uint64_t pd_table2[];
extern uint64_t pd_table3[];

struct mmu_root {
    uint64_t entry[512];
};

static struct mmu_root *kernel_root;

static inline uint64_t read_cr3(void)
{
    uint64_t val;
    __asm__ volatile("mov %%cr3, %0" : "=r"(val));
    return val;
}

static inline void write_cr3(uint64_t val)
{
    __asm__ volatile("mov %0, %%cr3" : : "r"(val) : "memory");
}

static inline void flush_tlb(void)
{
    __asm__ volatile("mov %%cr3, %%rax; mov %%rax, %%cr3" : : : "rax", "memory");
}

static uint64_t to_x86_flags(uint32_t flags)
{
    uint64_t x = X86_PTE_PRESENT;
    if (flags & MMU_WRITE)    x |= X86_PTE_WRITE;
    if (flags & MMU_USER)     x |= X86_PTE_USER;
    if (flags & MMU_HUGE)     x |= X86_PTE_HUGE;
    if (flags & MMU_GLOBAL)   x |= X86_PTE_GLOBAL;
    if (flags & MMU_UNCACHED) x |= X86_PTE_PWT | X86_PTE_PCD;
    if (flags & MMU_NX)       x |= X86_PTE_NX;
    return x;
}

static int split_huge(uint64_t *entry, uint64_t base)
{
    (void)base;
    uint64_t old = *entry;
    if (!(old & X86_PTE_PRESENT))
        return -1;
    if (!(old & X86_PTE_HUGE))
        return 0;

    uint64_t page = (uint64_t)pmm_alloc_frame();
    if (!page)
        return -1;

    uint64_t *pt = (uint64_t *)(uintptr_t)page;
    uint64_t phys = old & ~0xFFF;
    uint64_t flags = old & 0x1FF;
    flags &= ~X86_PTE_HUGE;
    flags &= ~X86_PTE_GLOBAL;

    for (int i = 0; i < 512; i++)
        pt[i] = (phys + i * PAGE_SIZE) | flags;

    *entry = page | (old & 0x1FF & ~X86_PTE_HUGE);
    flush_tlb();
    return 0;
}

static uint64_t *walk_page(struct mmu_root *root, uint64_t virt, int alloc)
{
    if (!root)
        root = kernel_root;

    uint64_t idx4 = (virt >> 39) & 0x1FF;
    uint64_t idx3 = (virt >> 30) & 0x1FF;
    uint64_t idx2 = (virt >> 21) & 0x1FF;
    uint64_t idx1 = (virt >> 12) & 0x1FF;

    uint64_t *entry = &root->entry[idx4];
    if (!(*entry & X86_PTE_PRESENT))
    {
        if (!alloc) return 0;
        uint64_t page = (uint64_t)pmm_alloc_frame();
        if (!page) return 0;
        uint64_t *zero = (uint64_t *)(uintptr_t)page;
        for (int i = 0; i < 512; i++)
            zero[i] = 0;
        *entry = page | X86_PTE_PRESENT | X86_PTE_WRITE | X86_PTE_USER;
    }

    uint64_t pdp_phys = *entry & ~0xFFF;
    uint64_t *pdp = (uint64_t *)(uintptr_t)pdp_phys;
    entry = &pdp[idx3];

    if (*entry & X86_PTE_HUGE)
    {
        uint64_t base = virt & ~((1ULL << 30) - 1);
        if (split_huge(entry, base) < 0)
            return 0;
    }

    if (!(*entry & X86_PTE_PRESENT))
    {
        if (!alloc) return 0;
        uint64_t page = (uint64_t)pmm_alloc_frame();
        if (!page) return 0;
        uint64_t *zero = (uint64_t *)(uintptr_t)page;
        for (int i = 0; i < 512; i++)
            zero[i] = 0;
        *entry = page | X86_PTE_PRESENT | X86_PTE_WRITE | X86_PTE_USER;
    }

    uint64_t pd_phys = *entry & ~0xFFF;
    uint64_t *pd = (uint64_t *)(uintptr_t)pd_phys;
    entry = &pd[idx2];

    if (*entry & X86_PTE_HUGE)
    {
        uint64_t base = virt & ~((1ULL << 21) - 1);
        if (split_huge(entry, base) < 0)
            return 0;
    }

    if (!(*entry & X86_PTE_PRESENT))
    {
        if (!alloc) return 0;
        uint64_t page = (uint64_t)pmm_alloc_frame();
        if (!page) return 0;
        uint64_t *zero = (uint64_t *)(uintptr_t)page;
        for (int i = 0; i < 512; i++)
            zero[i] = 0;
        *entry = page | X86_PTE_PRESENT | X86_PTE_WRITE | X86_PTE_USER;
    }

    uint64_t pt_phys = *entry & ~0xFFF;
    uint64_t *pt = (uint64_t *)(uintptr_t)pt_phys;
    return &pt[idx1];
}

struct mmu_root *mmu_current_root(void)
{
    return (struct mmu_root *)(uintptr_t)(read_cr3() & ~0xFFFULL);
}

struct mmu_root *mmu_kernel_root(void)
{
    return kernel_root;
}

void mmu_arch_init(void)
{
    kernel_root = (struct mmu_root *)(uintptr_t)read_cr3();

    uint64_t *pd = pd_table;
    for (uint64_t addr = 0x800000; addr < mmap_max_addr; addr += 0x200000)
    {
        int idx = addr >> 21;
        if (idx >= 512) break;
        if (pd[idx] & X86_PTE_PRESENT) continue;
        if (addr_in_reserved_region(addr))
            pd[idx] = addr | X86_PTE_PRESENT | X86_PTE_HUGE;
        else
            pd[idx] = addr | X86_PTE_PRESENT | X86_PTE_WRITE | X86_PTE_HUGE;
    }
    flush_tlb();

    pd = pd_table2;
    for (int idx = 0; idx < 512; idx++)
    {
        if (pd[idx] & X86_PTE_PRESENT) continue;
        uint64_t addr = 0x80000000ULL + idx * 0x200000ULL;
        if (addr_in_reserved_region(addr))
            pd[idx] = addr | X86_PTE_PRESENT | X86_PTE_HUGE;
        else
            pd[idx] = addr | X86_PTE_PRESENT | X86_PTE_WRITE | X86_PTE_HUGE;
    }

    pd = pd_table3;
    for (int idx = 0; idx < 512; idx++)
    {
        if (pd[idx] & X86_PTE_PRESENT) continue;
        uint64_t addr = 0xC0000000ULL + idx * 0x200000ULL;
        if (addr_in_reserved_region(addr))
            pd[idx] = addr | X86_PTE_PRESENT | X86_PTE_HUGE;
        else
            pd[idx] = addr | X86_PTE_PRESENT | X86_PTE_WRITE | X86_PTE_HUGE;
    }

    flush_tlb();

    uint64_t pdp_page = (uint64_t)pmm_alloc_frame();
    if (pdp_page)
    {
        uint64_t *pdp = (uint64_t *)(uintptr_t)pdp_page;
        for (int i = 0; i < 512; i++)
            pdp[i] = 0;
        kernel_root->entry[508] = pdp_page | X86_PTE_PRESENT | X86_PTE_WRITE;
        flush_tlb();
    }
}

int mmu_map(struct mmu_root *root, uint64_t virt, uint64_t phys, uint32_t flags)
{
    if (!root)
        root = kernel_root;
    uint64_t *pte = walk_page(root, virt, 1);
    if (!pte) return -1;
    if (*pte & X86_PTE_PRESENT)
        return -1;
    *pte = (phys & ~0xFFF) | to_x86_flags(flags);
    if (root == kernel_root)
        flush_tlb();
    return 0;
}

int mmu_unmap(struct mmu_root *root, uint64_t virt)
{
    if (!root)
        root = kernel_root;
    uint64_t *pte = walk_page(root, virt, 0);
    if (!pte || !(*pte & X86_PTE_PRESENT))
        return -1;
    *pte = 0;
    if (root == kernel_root)
        flush_tlb();
    return 0;
}

uint64_t mmu_virt_to_phys(struct mmu_root *root, uint64_t virt)
{
    if (!root)
        root = kernel_root;
    uint64_t *pte = walk_page(root, virt, 0);
    if (!pte || !(*pte & X86_PTE_PRESENT))
        return 0;
    return (*pte & ~0xFFF) | (virt & 0xFFF);
}

/* Non-allocating page-table walk: returns the PTE flags covering `virt`, or 0
   if any level is not present. Huge pages are accepted and reported as-is. */
static uint64_t range_pte(struct mmu_root *root, uint64_t virt)
{
    uint64_t idx4 = (virt >> 39) & 0x1FF;
    uint64_t *entry = &root->entry[idx4];
    if (!(*entry & X86_PTE_PRESENT))
        return 0;
    if (*entry & X86_PTE_HUGE)
        return *entry;

    uint64_t *pdp = (uint64_t *)(uintptr_t)(*entry & ~0xFFF);
    entry = &pdp[(virt >> 30) & 0x1FF];
    if (!(*entry & X86_PTE_PRESENT))
        return 0;
    if (*entry & X86_PTE_HUGE)
        return *entry;

    uint64_t *pd = (uint64_t *)(uintptr_t)(*entry & ~0xFFF);
    entry = &pd[(virt >> 21) & 0x1FF];
    if (!(*entry & X86_PTE_PRESENT))
        return 0;
    if (*entry & X86_PTE_HUGE)
        return *entry;

    uint64_t *pt = (uint64_t *)(uintptr_t)(*entry & ~0xFFF);
    entry = &pt[(virt >> 12) & 0x1FF];
    if (!(*entry & X86_PTE_PRESENT))
        return 0;
    return *entry;
}

int mmu_check_user_range(struct mmu_root *root, uint64_t virt, size_t len,
                         int write)
{
    if (!root)
        root = kernel_root;
    if (len == 0)
        return 1;
    if (virt > UINT64_MAX - len)
        return 0;

    uint64_t end = virt + len;
    while (virt < end)
    {
        uint64_t flags = range_pte(root, virt);
        if (!(flags & X86_PTE_PRESENT) || !(flags & X86_PTE_USER))
            return 0;
        if (write && !(flags & X86_PTE_WRITE))
            return 0;
        uint64_t step = PAGE_SIZE - (virt & (PAGE_SIZE - 1));
        uint64_t remain = end - virt;
        if (step > remain)
            step = remain;
        virt += step;
    }
    return 1;
}

int mmu_protect(struct mmu_root *root, uint64_t virt, uint32_t flags)
{
    if (!root)
        root = kernel_root;
    uint64_t *pte = walk_page(root, virt, 0);
    if (!pte || !(*pte & X86_PTE_PRESENT))
        return -1;
    uint64_t phys = *pte & ~0xFFF;
    *pte = phys | to_x86_flags(flags);
    /* Always invalidate: when the target root is the active one (e.g. the ELF
       loader hardening .text while the new address space is live), stale TLB
       entries must not keep granting the old permission bits. */
    flush_tlb();
    return 0;
}

static struct mmu_root *clone_level(struct mmu_root *src, int level, int deep_user)
{
    struct mmu_root *dst = (struct mmu_root *)(uintptr_t)pmm_alloc_frame();
    if (!dst)
        return 0;
    for (int i = 0; i < 512; i++)
        dst->entry[i] = 0;

    for (int i = 0; i < 512; i++)
    {
        uint64_t e = src->entry[i];
        if (!(e & X86_PTE_PRESENT))
            continue;

        int is_user = (e & X86_PTE_USER);

        if (!is_user)
        {
            dst->entry[i] = e;
            continue;
        }

        if (!deep_user)
        {
            dst->entry[i] = 0;
            continue;
        }

        if (level > 1)
        {
            if ((level == 2) && (e & X86_PTE_HUGE))
            {
                dst->entry[i] = e;
                continue;
            }
            uint64_t *next_src = (uint64_t *)(uintptr_t)(e & ~0xFFF);
            uint64_t *next_dst = (uint64_t *)(uintptr_t)clone_level(
                (struct mmu_root *)next_src, level - 1, deep_user);
            if (!next_dst)
                return 0;
            dst->entry[i] = (uint64_t)(uintptr_t)next_dst | (e & 0x1FF & ~X86_PTE_HUGE);
        }
        else
        {
            uint64_t *next_src = (uint64_t *)(uintptr_t)(e & ~0xFFF);
            uint64_t np = (uint64_t)pmm_alloc_frame();
            if (!np)
                return 0;
            __builtin_memcpy((void *)(uintptr_t)np, next_src, PAGE_SIZE);
            dst->entry[i] = np | (e & 0x1FF);
        }
    }
    return dst;
}

struct mmu_root *mmu_new_user_root(void)
{
    return clone_level(kernel_root, 4, 0);
}

struct mmu_root *mmu_clone_root(struct mmu_root *src)
{
    return clone_level(src, 4, 1);
}

static void free_level(uint64_t *tbl, int level)
{
    for (int i = 0; i < 512; i++)
    {
        uint64_t e = tbl[i];
        if (!(e & X86_PTE_PRESENT))
            continue;
        if (!(e & X86_PTE_USER))
            continue;
        if (level > 1)
        {
            if ((level == 2) && (e & X86_PTE_HUGE))
                continue;
            uint64_t *next = (uint64_t *)(uintptr_t)(e & ~0xFFF);
            free_level(next, level - 1);
            pmm_free_frame(next);
        }
        else
        {
            pmm_free_frame((void *)(uintptr_t)(e & ~0xFFF));
        }
    }
}

void mmu_free_root(struct mmu_root *root)
{
    if (!root || root == kernel_root)
        return;
    free_level((uint64_t *)root->entry, 4);
    pmm_free_frame(root);
}

void mmu_switch(struct mmu_root *root)
{
    if (!root)
        root = kernel_root;
    write_cr3((uint64_t)(uintptr_t)root);
}

void *mmu_map_framebuffer(uintptr_t phys, size_t size)
{
    uintptr_t start = phys & ~(uintptr_t)0x1FFFFF;
    uintptr_t end = phys + size;
    uintptr_t first_virt = 0;

    for (uintptr_t p = start; p < end; p += 0x200000)
    {
        unsigned int idx = (p >> 21) & 0x1FF;
        pd_table2[idx] = p | X86_PTE_PRESENT | X86_PTE_WRITE | X86_PTE_HUGE | X86_PTE_PWT;
        if (p == start)
            first_virt = 0x80000000ULL + idx * 0x200000ULL;
    }

    flush_tlb();
    return (void *)(first_virt + (phys & 0x1FFFFF));
}
