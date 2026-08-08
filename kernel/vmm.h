#ifndef AXIOME_VMM_H
#define AXIOME_VMM_H

#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE 4096
#define PAGE_SHIFT 12

#define USERSPACE_BASE 0x10000000000ULL

#define PTE_PRESENT  (1UL << 0)
#define PTE_WRITE    (1UL << 1)
#define PTE_USER     (1UL << 2)
#define PTE_ACCESSED (1UL << 5)
#define PTE_DIRTY    (1UL << 6)
#define PTE_HUGE     (1UL << 7)
#define PTE_GLOBAL   (1UL << 8)
#define PTE_PWT     (1UL << 3)
#define PTE_PCD     (1UL << 4)
#define PTE_NX       (1UL << 63)

#ifdef __cplusplus
extern "C" {
#endif

void vmm_init(void);
int vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags);
int vmm_map_page_in(uint64_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags);
int vmm_unmap_page(uint64_t virt);
uint64_t vmm_virt_to_phys(uint64_t virt);
int vmm_map_range(uint64_t virt, uint64_t phys, size_t pages, uint64_t flags);
void *vmm_mmap_phys(uint64_t phys, size_t pages, uint64_t flags);

uint64_t *vmm_kernel_pml4(void);
uint64_t *vmm_new_user_pml4(void);
uint64_t *vmm_clone_pml4(uint64_t *src);
void vmm_free_pml4(uint64_t *pml4);
void vmm_switch(uint64_t *pml4);

#ifdef __cplusplus
}
#endif

#endif
