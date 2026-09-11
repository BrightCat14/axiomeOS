#ifndef AXIOME_AXBOOT_H
#define AXIOME_AXBOOT_H

/* axboot handoff protocol (v1): the contract between the UEFI bootloader
   (bootloader/BOOTX64.EFI) and the native 64-bit kernel entry point.
   Included by both sides; keep freestanding-friendly (stdint only). */

#include <stdint.h>

#define AXIOME_BOOT_MAGIC 0x41584F4D45ULL /* "AXOME" */
#define AXBOOT_VERSION    1
#define AXBOOT_MAX_MMAP   64

/* Fixed physical address of struct axboot_info. Page-aligned, inside the
   kernel's early identity map so boot.S can read it before installing its
   own page tables, and below the kernel load address so kernel growth can
   never collide with it. 0x180000 sits in the conventional free block at
   1-2 MiB (0x1000000 is firmware BootServicesData under OVMF and cannot be
   reserved). The loader reserves this region in the memory map. */
#define AXBOOT_INFO_PHYS 0x180000ULL

/* Kernel image placement (must match linker.ld). */
#define AXBOOT_KERNEL_PHYS_LOAD 0x200000ULL
#define AXBOOT_KERNEL_VIRT_BASE 0xFFFFFFFF80000000ULL

enum axboot_pixel_format {
    AXB_PF_RGB_8_8_8 = 0, /* Red byte first - kernel's current expectation */
    AXB_PF_BGR_8_8_8 = 1,
};

/* Memory-map types. These intentionally reuse the kernel's numeric
   convention (kernel/mmap.h, pmm.c, mmu.c) so no translation is needed:
     1 = usable RAM
     2 = reserved
     3 = ACPI reclaimable
     4 = ACPI NVS
     5 = bad memory */
enum axboot_mem_type {
    AXB_MEM_USABLE = 1,
    AXB_MEM_RESERVED = 2,
    AXB_MEM_ACPI_RECLAIM = 3,
    AXB_MEM_ACPI_NVS = 4,
    AXB_MEM_BAD = 5,
};

struct axboot_mmap_entry {
    uint64_t base;
    uint64_t length;
    uint32_t type; /* enum axboot_mem_type */
    uint32_t reserved;
};

struct axboot_info {
    uint64_t magic;   /* AXIOME_BOOT_MAGIC */
    uint32_t version; /* AXBOOT_VERSION */
    uint32_t flags;   /* reserved, must be 0 */

    /* Memory map (physical). The array lives right after this struct at
       AXBOOT_INFO_PHYS; mmap points at it (physical address identity). */
    uint32_t                 mmap_count;
    uint32_t                 _pad0;
    struct axboot_mmap_entry *mmap;

    /* Framebuffer (GOP) */
    uint64_t fb_addr;
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t fb_pitch;
    uint32_t fb_bpp;
    uint32_t fb_pixel_format; /* enum axboot_pixel_format */

    /* ACPI / SMBIOS (physical addresses, 0 if absent) */
    uint64_t acpi_rsdp_addr;
    uint64_t smbios_addr;

    /* Kernel geometry (how the loader placed the image) */
    uint64_t kernel_phys_load; /* AXBOOT_KERNEL_PHYS_LOAD */
    uint64_t kernel_virt_base; /* AXBOOT_KERNEL_VIRT_BASE */
    uint64_t kernel_size;

    /* Boot disk hints (for real hardware; ignored under QEMU) */
    uint32_t boot_disk_bus;
    uint32_t boot_disk_drive;
    uint32_t boot_disk_part;

    /* Kernel command line (NUL-terminated, reserved for future use) */
    char cmdline[256];
};

#endif /* AXIOME_AXBOOT_H */
