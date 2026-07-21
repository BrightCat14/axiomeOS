#include "acpi.h"
#include "printk.h"
#include "string.h"

static struct acpi_rsdp *g_rsdp;
static struct acpi_madt *g_madt;

struct ioapic_info {
    uint64_t addr;
    uint32_t gsi_base;
};
static struct ioapic_info g_ioapics[8];
static int g_ioapic_count;

struct iso_info {
    uint8_t  irq;
    uint32_t gsi;
    uint16_t flags;
};
static struct iso_info g_isos[16];
static int g_iso_count;

static uint8_t acpi_checksum(const void *table, uint32_t length)
{
    uint8_t sum = 0;
    const uint8_t *p = (const uint8_t *)table;
    for (uint32_t i = 0; i < length; i++)
        sum += p[i];
    return sum;
}

static void *acpi_find_table(const char *signature)
{
    if (!g_rsdp || !g_rsdp->xsdt_addr)
        return 0;
    struct acpi_sdt_header *xsdt = (struct acpi_sdt_header *)(uintptr_t)g_rsdp->xsdt_addr;
    uint32_t entries = (xsdt->length - sizeof(*xsdt)) / 8;
    uint64_t *entry_ptr = (uint64_t *)((uintptr_t)xsdt + sizeof(*xsdt));
    for (uint32_t i = 0; i < entries; i++)
    {
        struct acpi_sdt_header *hdr = (struct acpi_sdt_header *)(uintptr_t)entry_ptr[i];
        if (__builtin_memcmp(hdr->signature, signature, 4) == 0)
            return hdr;
    }
    return 0;
}

void acpi_init(void *rsdp_addr)
{
    if (!rsdp_addr)
    {
        printk("ACPI: no RSDP\n");
        return;
    }
    g_rsdp = (struct acpi_rsdp *)rsdp_addr;
    if (acpi_checksum(g_rsdp, g_rsdp->revision > 0 ? 36 : 20) != 0)
    {
        printk("ACPI: RSDP checksum failed\n");
        g_rsdp = 0;
        return;
    }
    printk("ACPI: RSDP v%u oem=%.6s\n", g_rsdp->revision, g_rsdp->oem);

    struct acpi_sdt_header *xsdt = (struct acpi_sdt_header *)(uintptr_t)g_rsdp->xsdt_addr;
    if (!xsdt || __builtin_memcmp(xsdt->signature, "XSDT", 4) != 0)
    {
        printk("ACPI: XSDT not found\n");
        return;
    }
    if (acpi_checksum(xsdt, xsdt->length) != 0)
    {
        printk("ACPI: XSDT checksum failed\n");
        return;
    }

    g_madt = (struct acpi_madt *)acpi_find_table("APIC");
    if (!g_madt)
    {
        printk("ACPI: MADT not found\n");
        return;
    }
    if (acpi_checksum(g_madt, g_madt->header.length) != 0)
    {
        printk("ACPI: MADT checksum failed\n");
        g_madt = 0;
        return;
    }
    printk("ACPI: MADT lapic=0x%x flags=%u\n",
           g_madt->local_apic_addr, g_madt->flags);

    uint32_t remaining = g_madt->header.length - sizeof(*g_madt);
    uint8_t *ptr = g_madt->entries;
    while (remaining >= 2)
    {
        uint8_t type = ptr[0];
        uint8_t len  = ptr[1];
        if (len < 2 || len > remaining)
            break;
        switch (type)
        {
        case MADT_TYPE_IO_APIC:
        {
            struct madt_io_apic *ioapic = (struct madt_io_apic *)ptr;
            if (g_ioapic_count < 8)
            {
                g_ioapics[g_ioapic_count].addr = ioapic->ioapic_addr;
                g_ioapics[g_ioapic_count].gsi_base = ioapic->gsi_base;
                g_ioapic_count++;
                printk("ACPI: IOAPIC id=%u addr=0x%x gsi_base=%u\n",
                       ioapic->ioapic_id, ioapic->ioapic_addr, ioapic->gsi_base);
            }
            break;
        }
        case MADT_TYPE_ISO:
        {
            struct madt_iso *iso = (struct madt_iso *)ptr;
            if (g_iso_count < 16)
            {
                g_isos[g_iso_count].irq = iso->irq_src;
                g_isos[g_iso_count].gsi = iso->gsi;
                g_isos[g_iso_count].flags = iso->flags;
                g_iso_count++;
                printk("ACPI: ISO IRQ%u -> GSI%u flags=0x%x\n",
                       iso->irq_src, iso->gsi, iso->flags);
            }
            break;
        }
        }
        ptr += len;
        remaining -= len;
    }

    if (g_ioapic_count == 0)
        printk("ACPI: no IOAPICs found in MADT\n");
    else
        printk("ACPI: %d IOAPIC(s), %d ISO(s)\n", g_ioapic_count, g_iso_count);
}

int acpi_ioapic_count(void) { return g_ioapic_count; }

int acpi_ioapic_info(int idx, uint64_t *addr, uint32_t *gsi_base)
{
    if (idx < 0 || idx >= g_ioapic_count)
        return -1;
    if (addr) *addr = g_ioapics[idx].addr;
    if (gsi_base) *gsi_base = g_ioapics[idx].gsi_base;
    return 0;
}

int acpi_iso_lookup(uint8_t irq, uint32_t *gsi, uint16_t *flags)
{
    for (int i = 0; i < g_iso_count; i++)
    {
        if (g_isos[i].irq == irq)
        {
            if (gsi) *gsi = g_isos[i].gsi;
            if (flags) *flags = g_isos[i].flags;
            return 0;
        }
    }
    return -1;
}