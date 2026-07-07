#include "slab.h"
#include "pmm.h"
#include "vmm.h"
#include "printk.h"

#define SLAB_MIN_SHIFT 5
#define SLAB_MAX_SHIFT 10
#define SLAB_NUM (SLAB_MAX_SHIFT - SLAB_MIN_SHIFT + 1)

struct slab_header {
    uint32_t magic;
    struct slab_header *next;
    void *free_list;
    int obj_size;
    int free_count;
};

#define SLAB_MAGIC 0xDEADBEEF
#define BIG_MAGIC  0xB16B1E

struct slab_obj {
    uint32_t magic;
};

struct big_hdr {
    uint64_t magic;
    uint64_t npages;
    uint64_t phys_base;
};

static struct slab_header *slab_caches[SLAB_NUM];
static int slab_sizes[SLAB_NUM];

static int size_to_idx(size_t size)
{
    for (int i = 0; i < SLAB_NUM; i++)
    {
        if (size <= (size_t)slab_sizes[i])
            return i;
    }
    return -1;
}

static void slab_grow(int idx)
{
    uint64_t phys = (uint64_t)pmm_alloc_frame();
    if (!phys)
        return;

    /* Map the page into the kernel high-half. Using the raw physical
       address as a virtual address collides with identity-mapped kernel
       thread stacks (low physical frames), corrupting the slab free list. */
    void *vpage = vmm_mmap_phys(phys, 1, PTE_PRESENT | PTE_WRITE);
    if (!vpage)
    {
        pmm_free_frame((void *)(uintptr_t)phys);
        return;
    }

    struct slab_header *hdr = (struct slab_header *)vpage;
    int obj_size = slab_sizes[idx];
    hdr->magic = SLAB_MAGIC;
    int header_bytes = sizeof(struct slab_header);
    int data_start = (header_bytes + obj_size - 1) / obj_size * obj_size;
    int avail = PAGE_SIZE - data_start;
    int count = avail / obj_size;

    hdr->free_list = 0;
    hdr->obj_size = obj_size;
    hdr->free_count = 0;
    hdr->next = slab_caches[idx];

    void *cur = (void *)((uintptr_t)vpage + data_start);
    for (int i = 0; i < count; i++)
    {
        *(void **)cur = hdr->free_list;
        hdr->free_list = cur;
        hdr->free_count++;
        cur = (void *)((uintptr_t)cur + obj_size);
    }

    slab_caches[idx] = hdr;
}

void slab_init(void)
{
    for (int i = 0; i < SLAB_NUM; i++)
    {
        slab_sizes[i] = 1 << (SLAB_MIN_SHIFT + i);
        slab_caches[i] = 0;
        slab_grow(i);
    }
    printk("Slab: initialized %d caches (32..1024 bytes)\n", SLAB_NUM);
}

void *kmalloc(size_t size)
{
    if (size == 0)
        return 0;

    int idx = size_to_idx(size);
    if (idx < 0)
    {
        /* Large allocation: back it with whole pages. */
        uint64_t npages = (size + sizeof(struct big_hdr) + PAGE_SIZE - 1) / PAGE_SIZE;
        uint64_t phys = (uint64_t)pmm_alloc_frames(npages);
        if (!phys)
            return 0;
        void *v = vmm_mmap_phys(phys, npages, PTE_PRESENT | PTE_WRITE);
        if (!v)
        {
            pmm_free_frames((void *)(uintptr_t)phys, npages);
            return 0;
        }
        struct big_hdr *h = (struct big_hdr *)v;
        h->magic = BIG_MAGIC;
        h->npages = npages;
        h->phys_base = phys;
        return (void *)((uintptr_t)v + sizeof(struct big_hdr));
    }

    struct slab_header *hdr = slab_caches[idx];
    if (!hdr || hdr->free_count == 0)
    {
        slab_grow(idx);
        hdr = slab_caches[idx];
        if (!hdr || hdr->free_count == 0)
            return 0;
    }

    void *obj = hdr->free_list;
    hdr->free_list = *(void **)obj;
    hdr->free_count--;

    return obj;
}

void kfree(void *ptr)
{
    if (!ptr)
        return;

    uint64_t addr = (uint64_t)(uintptr_t)ptr;
    uint64_t page_base = addr & ~0xFFF;

    struct slab_header *hdr = (struct slab_header *)(uintptr_t)page_base;

    if (hdr->magic == BIG_MAGIC)
    {
        struct big_hdr *h = (struct big_hdr *)page_base;
        uint64_t npages = h->npages;
        uint64_t phys_base = h->phys_base;
        for (uint64_t i = 0; i < npages; i++)
            vmm_unmap_page(page_base + i * PAGE_SIZE);
        pmm_free_frames((void *)(uintptr_t)phys_base, npages);
        return;
    }

    *(void **)ptr = hdr->free_list;
    hdr->free_list = ptr;
    hdr->free_count++;
}
