#include "pci.h"
#include "io.h"
#include "slab.h"
#include "printk.h"
#include "string.h"
#include <stddef.h>

struct pci_device *g_pci_list;

uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off)
{
    uint32_t addr = 0x80000000u | ((uint32_t)bus << 16) |
                    ((uint32_t)dev << 11) | ((uint32_t)func << 8) | (off & 0xFC);
    outl(PCI_ADDR_PORT, addr);
    return inl(PCI_DATA_PORT);
}

void pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, uint32_t val)
{
    uint32_t addr = 0x80000000u | ((uint32_t)bus << 16) |
                    ((uint32_t)dev << 11) | ((uint32_t)func << 8) | (off & 0xFC);
    outl(PCI_ADDR_PORT, addr);
    outl(PCI_DATA_PORT, val);
}

static void pci_add(uint8_t bus, uint8_t dev, uint8_t func)
{
    uint32_t id = pci_read32(bus, dev, func, 0x00);
    uint16_t vendor = (uint16_t)(id & 0xFFFF);
    if (vendor == 0xFFFF)
        return;

    struct pci_device *p = (struct pci_device *)kmalloc(sizeof(struct pci_device));
    if (!p) return;
    memset(p, 0, sizeof(*p));

    uint32_t cls = pci_read32(bus, dev, func, 0x08);
    uint32_t hdr = pci_read32(bus, dev, func, 0x0C);
    uint32_t subsys = pci_read32(bus, dev, func, 0x2C);

    p->bus = bus; p->dev = dev; p->func = func;
    p->vendor = vendor;
    p->device = (uint16_t)(id >> 16);
    p->class_code = (uint8_t)(cls >> 24);
    p->subclass   = (uint8_t)(cls >> 16);
    p->prog_if    = (uint8_t)(cls >> 8);
    p->hdr_type   = (uint8_t)(hdr >> 16);
    (void)subsys;

    for (int i = 0; i < 6; i++)
        p->bar[i] = pci_read32(bus, dev, func, 0x10 + i * 4);

    /* Interrupt line (0x3C). */
    p->irq = (uint8_t)pci_read32(bus, dev, func, 0x3C);

    p->next = g_pci_list;
    g_pci_list = p;

    printk("PCI: bus=%d dev=%d func=%d vend=%x dev=%x class=%x subclass=%x irq=%d\n",
           (int)bus, (int)dev, (int)func, (int)vendor, (int)p->device,
           (int)p->class_code, (int)p->subclass, (int)p->irq);
}

static void pci_scan_bus(uint8_t bus)
{
    for (int dev = 0; dev < 32; dev++)
    {
        uint32_t id = pci_read32(bus, dev, 0, 0x00);
        if ((uint16_t)(id & 0xFFFF) == 0xFFFF)
            continue;
        uint32_t hdr = pci_read32(bus, dev, 0, 0x0C);
        int multifunc = (hdr >> 16) & 0x80;
        int max_func = multifunc ? 8 : 1;
        for (int func = 0; func < max_func; func++)
        {
            uint32_t fid = pci_read32(bus, dev, func, 0x00);
            if ((uint16_t)(fid & 0xFFFF) == 0xFFFF)
                continue;
            pci_add(bus, dev, func);

            /* Follow PCI-PCI bridges (class 0x06, subclass 0x04). */
            uint32_t cls = pci_read32(bus, dev, func, 0x08);
            if ((cls >> 24) == 0x06 && ((cls >> 16) & 0xFF) == 0x04)
            {
                uint32_t base = pci_read32(bus, dev, func, 0x18);
                uint8_t sec = (uint8_t)(base >> 8);
                if (sec && sec != bus)
                    pci_scan_bus(sec);
            }
        }
    }
}

void pci_init(void)
{
    g_pci_list = 0;
    printk("PCI: enumerating bus...\n");
    /* Check if the host bridge (0:0.0) is multifunction (PCI Express). */
    uint32_t hdr = pci_read32(0, 0, 0, 0x0C);
    if ((hdr >> 16) & 0x80)
        for (int func = 0; func < 8; func++)
            pci_add(0, 0, func);
    pci_scan_bus(0);
    printk("PCI: enumeration done\n");
}

struct pci_device *pci_first(void)
{
    return g_pci_list;
}

uint64_t pci_bar_addr(const struct pci_device *p, int idx)
{
    if (idx < 0 || idx > 5)
        return 0;
    uint32_t raw = p->bar[idx];
    if (raw == 0)
        return 0;
    if (raw & 1)
        return raw & 0xFFFC;          /* IO port */
    /* MMIO.  Bits 1-2 encode the BAR type: 0b10 = 64-bit, spanning this
       register and the next one (BAR idx+1 holds the upper 32 bits). */
    int type = (raw >> 1) & 0x3;
    if (type == 2)
    {
        uint32_t hi = (idx < 5) ? p->bar[idx + 1] : 0;
        return (((uint64_t)hi) << 32) | (uint64_t)(raw & 0xFFFFFFF0);
    }
    return (uint64_t)(raw & 0xFFFFFFF0);
}

/* ------------------------------------------------------------------ *
 * PCI Capability List
 * ------------------------------------------------------------------ */
uint8_t pci_find_cap(uint8_t bus, uint8_t dev, uint8_t func, uint8_t cap_id)
{
    uint32_t status = pci_read32(bus, dev, func, 0x04);
    if (!(status & (1 << 4)))
        return 0;
    uint8_t ptr = (uint8_t)(pci_read32(bus, dev, func, 0x34) & 0xFF);
    for (int i = 0; i < 48 && ptr; i++)
    {
        uint32_t cap = pci_read32(bus, dev, func, ptr);
        if ((cap & 0xFF) == cap_id)
            return ptr;
        ptr = (uint8_t)((cap >> 8) & 0xFF);
    }
    return 0;
}

uint8_t pci_find_cap_pdev(const struct pci_device *pdev, uint8_t cap_id)
{
    return pci_find_cap(pdev->bus, pdev->dev, pdev->func, cap_id);
}

/* ------------------------------------------------------------------ *
 * MSI Enable
 * Message Address = 0xFEE00000 | (apic_id << 12) | 0x0F00 (fixed, edge)
 * Message Data    = vector (edge-triggered, fixed delivery)
 * ------------------------------------------------------------------ */
int pci_msi_enable(uint8_t bus, uint8_t dev, uint8_t func, uint8_t vector)
{
    uint8_t cap = pci_find_cap(bus, dev, func, 0x05);
    if (!cap)
        return -1;

    uint32_t msg_addr = 0xFEE00000u | (8u << 12);  /* BSP APIC ID is usually 0 */
    uint16_t msg_data = vector;

    uint32_t ctrl = pci_read32(bus, dev, func, cap + 2) >> 16;

    int msi64 = (ctrl >> 7) & 1;  /* 64-bit capable? */
    int mmc   = (ctrl >> 1) & 7;  /* Multiple Message Capable */

    uint16_t flags = (uint16_t)pci_read32(bus, dev, func, cap);
    flags &= ~(7 << 1);
    flags |= (mmc << 1);
    flags |= 1;

    if (msi64)
    {
        pci_write32(bus, dev, func, cap + 4, msg_addr);
        pci_write32(bus, dev, func, cap + 8, 0);
        pci_write32(bus, dev, func, cap + 0xC, (uint32_t)msg_data);
    }
    else
    {
        pci_write32(bus, dev, func, cap + 4, msg_addr);
        pci_write32(bus, dev, func, cap + 8, (uint32_t)msg_data);
    }

    pci_write32(bus, dev, func, cap, flags);
    return 0;
}

/* ------------------------------------------------------------------ *
 * MSI-X Enable (single vector, table_index 0)
 * ------------------------------------------------------------------ */
int pci_msix_enable(uint8_t bus, uint8_t dev, uint8_t func, uint8_t vector,
                    uint64_t table_bar_phys, int table_index)
{
    uint8_t cap = pci_find_cap(bus, dev, func, 0x11);
    if (!cap)
        return -1;

    uint32_t bir = pci_read32(bus, dev, func, cap + 4);
    (void)bir;

    uint32_t msg_addr = 0xFEE00000u | (0u << 12);
    uint32_t msg_data = vector;

    volatile uint32_t *entry = (volatile uint32_t *)(uintptr_t)(table_bar_phys + table_index * 16);
    entry[0] = msg_addr;
    entry[1] = 0;
    entry[2] = msg_data;
    entry[3] = 1;

    uint16_t flags = (uint16_t)pci_read32(bus, dev, func, cap);
    flags |= (1 << 15);
    uint32_t cw = flags;
    cw |= ((uint32_t)pci_read32(bus, dev, func, cap + 4)) << 16;
    pci_write32(bus, dev, func, cap, cw & 0xFFFF);
    return 0;
}
