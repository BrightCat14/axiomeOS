#include "acpi.h"
#include "printk.h"
#include "string.h"

static struct acpi_rsdp *g_rsdp;
static int g_revision;
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
    if (!g_rsdp)
        return 0;

    uint32_t entry_count;
    uint64_t *entry_ptr;

    if (g_revision > 0)
    {
        if (!g_rsdp->xsdt_addr)
            return 0;
        struct acpi_sdt_header *xsdt = (struct acpi_sdt_header *)(uintptr_t)g_rsdp->xsdt_addr;
        if (__builtin_memcmp(xsdt->signature, "XSDT", 4) != 0)
            return 0;
        entry_count = (xsdt->length - sizeof(*xsdt)) / 8;
        entry_ptr = (uint64_t *)((uintptr_t)xsdt + sizeof(*xsdt));
    }
    else
    {
        if (!g_rsdp->rsdt_addr)
            return 0;
        struct acpi_sdt_header *rsdt = (struct acpi_sdt_header *)(uintptr_t)g_rsdp->rsdt_addr;
        if (__builtin_memcmp(rsdt->signature, "RSDT", 4) != 0)
            return 0;
        entry_count = (rsdt->length - sizeof(*rsdt)) / 4;
        uint32_t *entry32 = (uint32_t *)((uintptr_t)rsdt + sizeof(*rsdt));
        for (uint32_t i = 0; i < entry_count; i++)
        {
            struct acpi_sdt_header *hdr = (struct acpi_sdt_header *)(uintptr_t)entry32[i];
            if (__builtin_memcmp(hdr->signature, signature, 4) == 0)
                return hdr;
        }
        return 0;
    }

    for (uint32_t i = 0; i < entry_count; i++)
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
    if (__builtin_memcmp(g_rsdp->signature, "RSD PTR ", 8) != 0)
    {
        printk("ACPI: bad RSDP signature\n");
        g_rsdp = 0;
        return;
    }
    g_revision = g_rsdp->revision;
    uint32_t rsdp_len = g_revision > 0 ? 36 : 20;
    if (acpi_checksum(g_rsdp, rsdp_len) != 0)
    {
        printk("ACPI: RSDP checksum failed\n");
        g_rsdp = 0;
        return;
    }
    printk("ACPI: RSDP v%u oem=%c%c%c%c%c%c\n", g_revision,
           g_rsdp->oem[0], g_rsdp->oem[1], g_rsdp->oem[2],
           g_rsdp->oem[3], g_rsdp->oem[4], g_rsdp->oem[5]);

    if (g_revision > 0 && (!g_rsdp->xsdt_addr))
    {
        printk("ACPI: XSDT address is zero\n");
        g_rsdp = 0;
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