#ifndef AXIOME_ACPI_H
#define AXIOME_ACPI_H

#include <stdint.h>

struct acpi_rsdp {
    char     signature[8];
    uint8_t  checksum;
    char     oem[6];
    uint8_t  revision;
    uint32_t rsdt_addr;
    uint32_t length;
    uint64_t xsdt_addr;
    uint8_t  ext_checksum;
    uint8_t  reserved[3];
} __attribute__((packed));

struct acpi_sdt_header {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem[6];
    char     oem_table[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

struct acpi_madt {
    struct acpi_sdt_header header;
    uint32_t local_apic_addr;
    uint32_t flags;
    uint8_t  entries[];
} __attribute__((packed));

#define MADT_TYPE_LOCAL_APIC      0
#define MADT_TYPE_IO_APIC         1
#define MADT_TYPE_ISO             2
#define MADT_TYPE_NMI             4

struct madt_io_apic {
    uint8_t  type;
    uint8_t  length;
    uint8_t  ioapic_id;
    uint8_t  reserved;
    uint32_t ioapic_addr;
    uint32_t gsi_base;
} __attribute__((packed));

struct madt_iso {
    uint8_t  type;
    uint8_t  length;
    uint8_t  bus_src;
    uint8_t  irq_src;
    uint32_t gsi;
    uint16_t flags;
} __attribute__((packed));

void acpi_init(void *rsdp_addr);
int  acpi_ioapic_count(void);
int  acpi_ioapic_info(int idx, uint64_t *addr, uint32_t *gsi_base);
int  acpi_iso_lookup(uint8_t irq, uint32_t *gsi, uint16_t *flags);

#endif