#include "ioapic.h"
#include "printk.h"
#include "vmm.h"

#define IOAPIC_BASE 0xFEC00000

extern uint64_t pd_table3[512];

static volatile uint32_t *ioapic;

static uint32_t ioapic_read(unsigned int reg)
{
    ioapic[0] = reg;
    return ioapic[4];
}

static void ioapic_write(unsigned int reg, uint32_t val)
{
    ioapic[0] = reg;
    ioapic[4] = val;
}

void ioapic_set_entry(unsigned int gsi, uint8_t vector, uint8_t dest,
                      int masked)
{
    uint64_t entry = vector;
    entry |= (uint64_t)dest << 56;
    if (masked)
        entry |= (1ULL << 16);

    ioapic_write(0x10 + gsi * 2, entry & 0xFFFFFFFF);
    ioapic_write(0x10 + gsi * 2 + 1, entry >> 32);
}

void ioapic_mask(unsigned int gsi, int mask)
{
    uint64_t entry = ioapic_read(0x10 + gsi * 2);
    entry |= (uint64_t)ioapic_read(0x10 + gsi * 2 + 1) << 32;
    if (mask)
        entry |= (1ULL << 16);
    else
        entry &= ~(1ULL << 16);
    ioapic_write(0x10 + gsi * 2, entry & 0xFFFFFFFF);
    ioapic_write(0x10 + gsi * 2 + 1, entry >> 32);
}

void ioapic_init(void)
{
    unsigned int pd_idx = (IOAPIC_BASE >> 21) & 0x1FF;
    pd_table3[pd_idx] = IOAPIC_BASE | PTE_PRESENT | PTE_WRITE | PTE_HUGE | PTE_PCD | PTE_PWT;
    __asm__ volatile("mov %%cr3, %%rax; mov %%rax, %%cr3" ::: "rax");

    ioapic = (volatile uint32_t *)IOAPIC_BASE;

    uint32_t version = ioapic_read(1);
    unsigned int max_entries = ((version >> 16) & 0xFF) + 1;

    printk("IOAPIC: version=0x%x max_GSI=%u\n", version & 0xFF, max_entries);

    for (unsigned int i = 0; i < max_entries; i++)
        ioapic_set_entry(i, 0, 0, 1);

    ioapic_set_entry(1, 0x21, 0, 1);
    ioapic_set_entry(12, 0x22, 0, 1);
    ioapic_set_entry(4, 0x24, 0, 1);

    printk("IOAPIC: initialized\n");
}
