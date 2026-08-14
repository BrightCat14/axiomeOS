#include "vmm.h"
#include "arch_mmu.h"
#include "printk.h"
#include "mmap.h"

extern uint64_t mmap_max_addr;

/* Portable virtual-memory policy on top of the arch MMU. Page-table format,
   TLB management and the root table layout live in arch/x86_64/mmu.c. */

void vmm_init(void)
{
    mmu_arch_init();
    printk("VMM: init done, root=%p max=0x%lx\n",
           (void *)vmm_kernel_root(), mmap_max_addr);
}

int vmm_map_page(uint64_t virt, uint64_t phys, uint32_t flags)
{
    return mmu_map(0, virt, phys, flags);
}

int vmm_map_page_in(struct mmu_root *root, uint64_t virt, uint64_t phys,
                    uint32_t flags)
{
    return mmu_map(root, virt, phys, flags);
}

int vmm_unmap_page(uint64_t virt)
{
    return mmu_unmap(0, virt);
}

int vmm_protect_page(struct mmu_root *root, uint64_t virt, uint32_t flags)
{
    return mmu_protect(root, virt, flags);
}

int vmm_check_user_range(struct mmu_root *root, uint64_t virt, size_t len,
                         int write)
{
    return mmu_check_user_range(root, virt, len, write);
}

uint64_t vmm_virt_to_phys(uint64_t virt)
{
    return mmu_virt_to_phys(0, virt);
}

uint64_t vmm_virt_to_phys_in(struct mmu_root *root, uint64_t virt)
{
    return mmu_virt_to_phys(root, virt);
}

int vmm_map_range(uint64_t virt, uint64_t phys, size_t pages, uint32_t flags)
{
    for (size_t i = 0; i < pages; i++)
    {
        if (vmm_map_page(virt + i * PAGE_SIZE, phys + i * PAGE_SIZE, flags) < 0)
            return -1;
    }
    return 0;
}

void *vmm_mmap_phys(uint64_t phys, size_t pages, uint32_t flags)
{
    static uint64_t next_offset;
    uint64_t virt = 0xFFFFFE0000000000ULL + next_offset;
    if (vmm_map_range(virt, phys, pages, flags) < 0)
        return 0;
    next_offset += pages * PAGE_SIZE;
    return (void *)virt;
}

struct mmu_root *vmm_kernel_root(void)
{
    return mmu_kernel_root();
}

struct mmu_root *vmm_new_user_root(void)
{
    return mmu_new_user_root();
}

struct mmu_root *vmm_clone_root(struct mmu_root *src)
{
    return mmu_clone_root(src);
}

void vmm_free_root(struct mmu_root *root)
{
    mmu_free_root(root);
}

void vmm_switch(struct mmu_root *root)
{
    mmu_switch(root);
}

struct mmu_root *vmm_current_root(void)
{
    return mmu_current_root();
}
