#ifndef AXIOME_MMAP_H
#define AXIOME_MMAP_H

#include <stdint.h>

#define MAX_MMAP_ENTRIES 64

struct mmap_entry {
    uint64_t base;
    uint64_t length;
    uint32_t type;
};

struct mmap_info {
    int count;
    struct mmap_entry entries[MAX_MMAP_ENTRIES];
};

extern struct mmap_info kernel_mmap;
extern void *acpi_rsdp_addr;

static inline int addr_in_reserved_region(uint64_t addr)
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

#endif
