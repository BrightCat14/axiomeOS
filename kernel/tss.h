#ifndef AXIOME_TSS_H
#define AXIOME_TSS_H

#include <stdint.h>

struct tss
{
    uint32_t reserved0;
    uint64_t rsp[3];
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

extern struct tss tss;

void tss_init(void);
void tss_set_rsp0(uint64_t rsp0);

#endif
