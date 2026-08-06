#ifndef AXIOME_PCI_H
#define AXIOME_PCI_H

#include <stdint.h>

#define PCI_ADDR_PORT 0xCF8
#define PCI_DATA_PORT 0xCFC

struct pci_device {
    uint8_t bus;
    uint8_t dev;
    uint8_t func;
    uint16_t vendor;
    uint16_t device;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t hdr_type;
    uint8_t irq;
    uint32_t bar[6];
    struct pci_device *next;
};

/* Read a 32-bit configuration dword for (bus,dev,func) at offset `off`. */
uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off);

/* Write a 32-bit configuration dword. */
void pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, uint32_t val);

/* Enumerate the PCI bus(es) and print discovered devices. */
void pci_init(void);

/* Walk the discovered device list (NULL-terminated). */
struct pci_device *pci_first(void);

/* Physical address of BAR `idx` (MMIO address or IO port). 0 if unimplemented. */
uint64_t pci_bar_addr(const struct pci_device *p, int idx);

/* Capability list */
uint8_t pci_find_cap(uint8_t bus, uint8_t dev, uint8_t func, uint8_t cap_id);
uint8_t pci_find_cap_pdev(const struct pci_device *pdev, uint8_t cap_id);

/* MSI / MSI-X */
int  pci_msi_enable(uint8_t bus, uint8_t dev, uint8_t func, uint8_t vector);
int  pci_msix_enable(uint8_t bus, uint8_t dev, uint8_t func, uint8_t vector,
                     uint64_t table_bar_phys, int table_index);

#endif
