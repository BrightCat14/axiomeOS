#ifndef AXIOME_VMM_H
#define AXIOME_VMM_H

#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE 4096
#define PAGE_SHIFT 12

#define USERSPACE_BASE 0x10000000000ULL

/* Architecture-independent page flags (translated by the arch MMU). */
#define MMU_WRITE    (1u << 0)
#define MMU_USER     (1u << 1)
#define MMU_NX       (1u << 2)
#define MMU_HUGE     (1u << 3)
#define MMU_GLOBAL   (1u << 4)
#define MMU_UNCACHED (1u << 5)

#ifdef __cplusplus
extern "C" {
#endif

/* Portable address-space handle. Opaque; only the arch MMU dereferences it. */
struct mmu_root;

void vmm_init(void);
int vmm_map_page(uint64_t virt, uint64_t phys, uint32_t flags);
int vmm_map_page_in(struct mmu_root *root, uint64_t virt, uint64_t phys,
                    uint32_t flags);
int vmm_unmap_page(uint64_t virt);
uint64_t vmm_virt_to_phys(uint64_t virt);
int vmm_map_range(uint64_t virt, uint64_t phys, size_t pages, uint32_t flags);
void *vmm_mmap_phys(uint64_t phys, size_t pages, uint32_t flags);

struct mmu_root *vmm_kernel_root(void);
struct mmu_root *vmm_new_user_root(void);
struct mmu_root *vmm_clone_root(struct mmu_root *src);
void vmm_free_root(struct mmu_root *root);
void vmm_switch(struct mmu_root *root);
struct mmu_root *vmm_current_root(void);

#ifdef __cplusplus
}
#endif

#endif
