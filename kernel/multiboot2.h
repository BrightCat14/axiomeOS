#ifndef AXIOME_MULTIBOOT2_H
#define AXIOME_MULTIBOOT2_H

#include <stdint.h>

#define MULTIBOOT2_MAGIC        0xE85250D6
#define MULTIBOOT2_BOOT_MAGIC   0x36D76289

#define MULTIBOOT2_TAG_END           0
#define MULTIBOOT2_TAG_CMDLINE       1
#define MULTIBOOT2_TAG_MODULE        3
#define MULTIBOOT2_TAG_MEMORY        4
#define MULTIBOOT2_TAG_MMAP          6
#define MULTIBOOT2_TAG_FRAMEBUFFER   8
#define MULTIBOOT2_TAG_ACPI_RSDP     14
#define MULTIBOOT2_TAG_SMBIOS        15

struct multiboot2_info {
    uint32_t total_size;
    uint32_t reserved;
};

struct multiboot2_tag {
    uint32_t type;
    uint32_t size;
};

struct multiboot2_tag_mmap_entry {
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
};

struct multiboot2_tag_mmap {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    struct multiboot2_tag_mmap_entry entries[];
};

struct multiboot2_tag_framebuffer {
    uint32_t type;
    uint32_t size;
    uint64_t fb_addr;
    uint32_t fb_pitch;
    uint32_t fb_width;
    uint32_t fb_height;
    uint8_t  fb_bpp;
    uint8_t  fb_type;
    uint8_t  reserved;
};

struct multiboot2_tag_acpi_rsdp {
    uint32_t type;
    uint32_t size;
    uint8_t  rsdp[1];
};

struct multiboot2_tag_smbios {
    uint32_t type;
    uint32_t size;
    uint8_t  major;
    uint8_t  minor;
    uint8_t  reserved[6];
    uint8_t  tables[];
};

#endif
