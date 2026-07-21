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

#endif
