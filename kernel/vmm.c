#include "vmm.h"
#include "pmm.h"
#include "printk.h"
#include "mmap.h"

extern uint64_t mmap_max_addr;
extern uint64_t pd_table[];

static uint64_t *kernel_pml4;

static inline uint64_t read_cr3(void)
{
    uint64_t val;
    __asm__ volatile("mov %%cr3, %0" : "=r"(val));
    return val;
}

static inline void flush_tlb(void)
{
    __asm__ volatile("mov %%cr3, %%rax; mov %%rax, %%cr3" : : : "rax", "memory");
}

static int split_huge(uint64_t *entry, uint64_t base)
{
    uint64_t old = *entry;
    if (!(old & PTE_PRESENT))
        return -1;
    if (!(old & PTE_HUGE))
        return 0;

    uint64_t page = (uint64_t)pmm_alloc_frame();
    if (!page)
        return -1;

    uint64_t *pt = (uint64_t *)(uintptr_t)page;
    uint64_t phys = old & ~0xFFF;
    uint64_t flags = old & 0x1FF;
    flags &= ~PTE_HUGE;
    flags &= ~PTE_GLOBAL;

    for (int i = 0; i < 512; i++)
        pt[i] = (phys + i * PAGE_SIZE) | flags;

    *entry = page | (old & 0x1FF & ~PTE_HUGE);
    flush_tlb();
    return 0;
}

static uint64_t *walk_page(uint64_t *pml4, uint64_t virt, int alloc)
{
    if (!pml4)
        pml4 = kernel_pml4;

    uint64_t idx4 = (virt >> 39) & 0x1FF;
    uint64_t idx3 = (virt >> 30) & 0x1FF;
    uint64_t idx2 = (virt >> 21) & 0x1FF;
    uint64_t idx1 = (virt >> 12) & 0x1FF;

    uint64_t *entry = &pml4[idx4];
    if (!(*entry & PTE_PRESENT))
    {
        if (!alloc) return 0;
        uint64_t page = (uint64_t)pmm_alloc_frame();
        if (!page) return 0;
        uint64_t *zero = (uint64_t *)(uintptr_t)page;
        for (int i = 0; i < 512; i++)
            zero[i] = 0;
        *entry = page | PTE_PRESENT | PTE_WRITE | PTE_USER;
    }

    uint64_t pdp_phys = *entry & ~0xFFF;
    uint64_t *pdp = (uint64_t *)(uintptr_t)pdp_phys;
    entry = &pdp[idx3];

    if (*entry & PTE_HUGE)
    {
        uint64_t base = virt & ~((1ULL << 30) - 1);
        if (split_huge(entry, base) < 0)
            return 0;
    }

    if (!(*entry & PTE_PRESENT))
    {
        if (!alloc) return 0;
        uint64_t page = (uint64_t)pmm_alloc_frame();
        if (!page) return 0;
        uint64_t *zero = (uint64_t *)(uintptr_t)page;
        for (int i = 0; i < 512; i++)
            zero[i] = 0;
        *entry = page | PTE_PRESENT | PTE_WRITE | PTE_USER;
    }

    uint64_t pd_phys = *entry & ~0xFFF;
    uint64_t *pd = (uint64_t *)(uintptr_t)pd_phys;
    entry = &pd[idx2];

    if (*entry & PTE_HUGE)
    {
        uint64_t base = virt & ~((1ULL << 21) - 1);
        if (split_huge(entry, base) < 0)
            return 0;
    }

    if (!(*entry & PTE_PRESENT))
    {
        if (!alloc) return 0;
        uint64_t page = (uint64_t)pmm_alloc_frame();
        if (!page) return 0;
        uint64_t *zero = (uint64_t *)(uintptr_t)page;
        for (int i = 0; i < 512; i++)
            zero[i] = 0;
        *entry = page | PTE_PRESENT | PTE_WRITE | PTE_USER;
    }

    uint64_t pt_phys = *entry & ~0xFFF;
    uint64_t *pt = (uint64_t *)(uintptr_t)pt_phys;
    return &pt[idx1];
}

static int addr_in_reserved_region(uint64_t addr)
{
    for (int i = 0; i < kernel_mmap.count; i++)
    {
        if (kernel_mmap.entries[i].type == 1)
            continue;
        uint64_t base = kernel_mmap.entries[i].base;
        uint64_t end = base + kernel_mmap.entries[i].length;
        if (addr >= base && addr < end)
            return 1;
    }
    return 0;
}

void vmm_init(void)
{
    kernel_pml4 = (uint64_t *)(uintptr_t)read_cr3();

    uint64_t *pd = pd_table;
    for (uint64_t addr = 0x800000; addr < mmap_max_addr; addr += 0x200000)
    {
        int idx = addr >> 21;
        if (idx >= 512) break;
        if (pd[idx] & PTE_PRESENT) continue;
        if (addr_in_reserved_region(addr))
            pd[idx] = addr | PTE_PRESENT | PTE_HUGE;
        else
            pd[idx] = addr | PTE_PRESENT | PTE_WRITE | PTE_HUGE;
    }
    flush_tlb();

    uint64_t pdp_page = (uint64_t)pmm_alloc_frame();
    if (pdp_page)
    {
        uint64_t *pdp = (uint64_t *)(uintptr_t)pdp_page;
        for (int i = 0; i < 512; i++)
            pdp[i] = 0;
        kernel_pml4[508] = pdp_page | PTE_PRESENT | PTE_WRITE;
        flush_tlb();
    }

    printk("VMM: init done, PML4=%p max=0x%lx\n", (void*)read_cr3(), mmap_max_addr);
}

int vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags)
{
    uint64_t *pte = walk_page(kernel_pml4, virt, 1);
    if (!pte) return -1;
    if (*pte & PTE_PRESENT)
        return -1;
    *pte = (phys & ~0xFFF) | flags | PTE_PRESENT;
    flush_tlb();
    return 0;
}

int vmm_unmap_page(uint64_t virt)
{
    uint64_t *pte = walk_page(kernel_pml4, virt, 0);
    if (!pte || !(*pte & PTE_PRESENT))
        return -1;
    *pte = 0;
    flush_tlb();
    return 0;
}

uint64_t vmm_virt_to_phys(uint64_t virt)
{
    uint64_t *pte = walk_page(kernel_pml4, virt, 0);
    if (!pte || !(*pte & PTE_PRESENT))
        return 0;
    return (*pte & ~0xFFF) | (virt & 0xFFF);
}

int vmm_map_range(uint64_t virt, uint64_t phys, size_t pages, uint64_t flags)
{
    for (size_t i = 0; i < pages; i++)
    {
        if (vmm_map_page(virt + i * PAGE_SIZE, phys + i * PAGE_SIZE, flags) < 0)
            return -1;
    }
    return 0;
}

void *vmm_mmap_phys(uint64_t phys, size_t pages, uint64_t flags)
{
    static uint64_t next_offset;
    uint64_t virt = 0xFFFFFE0000000000ULL + next_offset;
    if (vmm_map_range(virt, phys, pages, flags) < 0)
        return 0;
    next_offset += pages * PAGE_SIZE;
    return (void *)virt;
}

uint64_t *vmm_kernel_pml4(void)
{
    return kernel_pml4;
}

static uint64_t *clone_level(uint64_t *src, int level, int deep_user)
{
    /* level: 4=PML4, 3=PDP, 2=PD, 1=PT. Kernel entries (no PTE_USER) are
       shared verbatim; user entries are cleared (spawn) or deep-copied (fork). */
    uint64_t *dst = (uint64_t *)(uintptr_t)pmm_alloc_frame();
    if (!dst)
        return 0;
    for (int i = 0; i < 512; i++)
        dst[i] = 0;

    for (int i = 0; i < 512; i++)
    {
        uint64_t e = src[i];
        if (!(e & PTE_PRESENT))
            continue;

        int is_user = (e & PTE_USER);

        if (!is_user)
        {
            dst[i] = e;
            continue;
        }

        if (!deep_user)
        {
            dst[i] = 0;
            continue;
        }

        if (level > 1)
        {
            if ((level == 2) && (e & PTE_HUGE))
            {
                dst[i] = e;
                continue;
            }
            uint64_t *next_src = (uint64_t *)(uintptr_t)(e & ~0xFFF);
            uint64_t *next_dst = clone_level(next_src, level - 1, deep_user);
            if (!next_dst)
                return 0;
            dst[i] = (uint64_t)(uintptr_t)next_dst | (e & 0x1FF & ~PTE_HUGE);
        }
        else
        {
            uint64_t *next_src = (uint64_t *)(uintptr_t)(e & ~0xFFF);
            uint64_t np = (uint64_t)pmm_alloc_frame();
            if (!np)
                return 0;
            __builtin_memcpy((void *)(uintptr_t)np, next_src, PAGE_SIZE);
            dst[i] = np | (e & 0x1FF);
        }
    }
    return dst;
}

uint64_t *vmm_new_user_pml4(void)
{
    return clone_level(kernel_pml4, 4, 0);
}

uint64_t *vmm_clone_pml4(uint64_t *src)
{
    return clone_level(src, 4, 1);
}

static void free_level(uint64_t *tbl, int level)
{
    for (int i = 0; i < 512; i++)
    {
        uint64_t e = tbl[i];
        if (!(e & PTE_PRESENT))
            continue;
        if (!(e & PTE_USER))
            continue;
        if (level > 1)
        {
            if ((level == 2) && (e & PTE_HUGE))
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

void vmm_free_pml4(uint64_t *pml4)
{
    if (!pml4 || pml4 == kernel_pml4)
        return;
    free_level(pml4, 4);
    pmm_free_frame(pml4);
}

void vmm_switch(uint64_t *pml4)
{
    if (!pml4)
        pml4 = kernel_pml4;
    __asm__ volatile("mov %0, %%cr3" : : "r"((uint64_t)(uintptr_t)pml4) : "memory");
}

int vmm_map_page_in(uint64_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags)
{
    uint64_t *pte = walk_page(pml4, virt, 1);
    if (!pte)
        return -1;
    if (*pte & PTE_PRESENT)
        return -1;
    *pte = (phys & ~0xFFF) | flags | PTE_PRESENT;
    return 0;
}
