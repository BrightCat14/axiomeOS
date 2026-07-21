#include "ioapic.h"
#include "printk.h"
#include "vmm.h"
#include "acpi.h"

extern uint64_t pd_table3[512];

struct ioapic_regs {
    volatile uint32_t *base;
    uint32_t gsi_base;
    uint32_t gsi_end;
};

#define MAX_IOAPICS 8
static struct ioapic_regs g_ioapics[MAX_IOAPICS];
static int g_ioapic_count;

static uint32_t ioapic_read(volatile uint32_t *base, unsigned int reg)
{
    base[0] = reg;
    return base[4];
}

static void ioapic_write(volatile uint32_t *base, unsigned int reg, uint32_t val)
{
    base[0] = reg;
    base[4] = val;
}

static int ioapic_for_gsi(unsigned int gsi, volatile uint32_t **base_out, int *pin_out)
{
    for (int i = 0; i < g_ioapic_count; i++)
    {
        if (gsi >= g_ioapics[i].gsi_base && gsi <= g_ioapics[i].gsi_end)
        {
            if (base_out) *base_out = g_ioapics[i].base;
            if (pin_out)  *pin_out = (int)(gsi - g_ioapics[i].gsi_base);
            return 0;
        }
    }
    return -1;
}

static void map_ioapic_mmio(uint64_t phys)
{
    unsigned int pd_idx = (phys >> 21) & 0x1FF;
    pd_table3[pd_idx] = phys | PTE_PRESENT | PTE_WRITE | PTE_HUGE | PTE_PCD | PTE_PWT;
    __asm__ volatile("mov %%cr3, %%rax; mov %%rax, %%cr3" ::: "rax");
}

void ioapic_set_entry(unsigned int gsi, uint8_t vector, uint8_t dest,
                      int masked)
{
    volatile uint32_t *base;
    int pin;
    if (ioapic_for_gsi(gsi, &base, &pin) != 0)
    {
        printk("IOAPIC: no IOAPIC for GSI %u\n", gsi);
        return;
    }
    uint64_t entry = vector;
    entry |= (uint64_t)dest << 56;
    if (masked)
        entry |= (1ULL << 16);

    ioapic_write(base, 0x10 + pin * 2, entry & 0xFFFFFFFF);
    ioapic_write(base, 0x10 + pin * 2 + 1, entry >> 32);
}

void ioapic_mask(unsigned int gsi, int mask)
{
    volatile uint32_t *base;
    int pin;
    if (ioapic_for_gsi(gsi, &base, &pin) != 0)
        return;
    uint64_t entry = ioapic_read(base, 0x10 + pin * 2);
    entry |= (uint64_t)ioapic_read(base, 0x10 + pin * 2 + 1) << 32;
    if (mask)
        entry |= (1ULL << 16);
    else
        entry &= ~(1ULL << 16);
    ioapic_write(base, 0x10 + pin * 2, entry & 0xFFFFFFFF);
    ioapic_write(base, 0x10 + pin * 2 + 1, entry >> 32);
}

void ioapic_init(void)
{
    int n = acpi_ioapic_count();
    if (n == 0)
    {
        printk("IOAPIC: no MADT entries, falling back to legacy 0xFEC00000\n");
        map_ioapic_mmio(0xFEC00000);
        g_ioapics[0].base = (volatile uint32_t *)0xFEC00000;
        g_ioapics[0].gsi_base = 0;
        g_ioapic_count = 1;
        uint32_t ver = ioapic_read(g_ioapics[0].base, 1);
        g_ioapics[0].gsi_end = ((ver >> 16) & 0xFF);
        printk("IOAPIC: legacy fallback version=0x%x max_GSI=%u\n",
               ver & 0xFF, g_ioapics[0].gsi_end + 1);
    }
    else
    {
        for (int i = 0; i < n && i < MAX_IOAPICS; i++)
        {
            uint64_t addr;
            uint32_t gsi_base;
            acpi_ioapic_info(i, &addr, &gsi_base);
            map_ioapic_mmio(addr);
            g_ioapics[g_ioapic_count].base = (volatile uint32_t *)(uintptr_t)addr;
            g_ioapics[g_ioapic_count].gsi_base = gsi_base;
            uint32_t ver = ioapic_read(g_ioapics[g_ioapic_count].base, 1);
            unsigned int max_redirs = ((ver >> 16) & 0xFF);
            g_ioapics[g_ioapic_count].gsi_end = gsi_base + max_redirs;
            printk("IOAPIC[%d]: MADT addr=0x%lx gsi_base=%u version=0x%x max_GSI=%u\n",
                   i, addr, gsi_base, ver & 0xFF, max_redirs + 1);
            g_ioapic_count++;
        }
    }

    for (int i = 0; i < g_ioapic_count; i++)
    {
        unsigned int max_pins = g_ioapics[i].gsi_end - g_ioapics[i].gsi_base;
        for (unsigned int p = 0; p <= max_pins; p++)
        {
            ioapic_write(g_ioapics[i].base, 0x10 + p * 2, (1 << 16));
            ioapic_write(g_ioapics[i].base, 0x10 + p * 2 + 1, 0);
        }
    }

    {
        uint32_t gsi = 1;
        uint16_t flags = 0;
        if (acpi_iso_lookup(1, &gsi, &flags) != 0)
            gsi = 1;
        ioapic_set_entry(gsi, 0x21, 0, 1);
        printk("IOAPIC: keyboard GSI=%u\n", gsi);
    }
    {
        uint32_t gsi = 12;
        uint16_t flags = 0;
        if (acpi_iso_lookup(12, &gsi, &flags) != 0)
            gsi = 12;
        ioapic_set_entry(gsi, 0x22, 0, 1);
        printk("IOAPIC: mouse GSI=%u\n", gsi);
    }
    {
        uint32_t gsi = 4;
        uint16_t flags = 0;
        if (acpi_iso_lookup(4, &gsi, &flags) != 0)
            gsi = 4;
        ioapic_set_entry(gsi, 0x24, 0, 1);
        printk("IOAPIC: serial GSI=%u\n", gsi);
    }

    printk("IOAPIC: initialized (%d controller(s))\n", g_ioapic_count);
}

void ioapic_set_entry_flags(unsigned int gsi, uint8_t vector, uint8_t dest,
                            int masked, uint16_t flags)
{
    volatile uint32_t *base;
    int pin;
    if (ioapic_for_gsi(gsi, &base, &pin) != 0)
        return;
    uint64_t entry = vector;
    entry |= (uint64_t)dest << 56;
    if (masked)
        entry |= (1ULL << 16);
    entry |= (uint64_t)(flags & 3) << 13;
    entry |= (uint64_t)((flags >> 2) & 1) << 15;

    ioapic_write(base, 0x10 + pin * 2, entry & 0xFFFFFFFF);
    ioapic_write(base, 0x10 + pin * 2 + 1, entry >> 32);
}