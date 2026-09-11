/* axboot UEFI bootloader: shared internal declarations. */

#ifndef AXBOOTLOADER_H
#define AXBOOTLOADER_H

#include <efi.h>
#include <efilib.h>

#include "axboot.h"

/* Number of pages reserved at AXBOOT_INFO_PHYS for struct axboot_info plus
   the inline mmap array (struct ~0.5KB + 64 entries * 24B ~= 2KB). */
#define BOOTINFO_PAGES 4

/* Temporary stack for the kernel handoff (post-ExitBootServices). */
#define HANDOFF_STACK_PAGES 4

struct boot_ctx {
    EFI_HANDLE         image;
    EFI_SYSTEM_TABLE  *st;
    EFI_BOOT_SERVICES *bs;

    /* GOP framebuffer capture */
    UINT64 fb_addr;
    UINT32 fb_width;
    UINT32 fb_height;
    UINT32 fb_pitch;
    UINT32 fb_bpp;
    UINT32 fb_pf; /* enum axboot_pixel_format */
    int    fb_valid;

    /* ACPI / SMBIOS (physical addresses, 0 if absent) */
    UINT64 acpi_rsdp;
    UINT64 smbios;

    /* Loaded kernel image */
    UINT8 *file_data;
    UINTN  file_size;
    UINT64 kern_entry;
    UINT64 kern_phys; /* lowest PT_LOAD p_paddr */
    UINT64 kern_size; /* highest PT_LOAD end - kern_phys */

    /* Loader-owned physical ranges (reserved in the axboot mmap) */
    UINT64 info_phys;
    UINT64 info_pages;
    UINT64 tables_phys;
    UINT64 tables_pages;
    UINT64 stack_phys;
    UINT64 stack_pages;

    /* Final memory map (valid until ExitBootServices) */
    EFI_MEMORY_DESCRIPTOR *memmap;
    UINTN                  memmap_size;
    UINTN                  desc_size;
    UINT32                 desc_ver;
    UINTN                  memmap_key;
    UINTN                  n_desc;
};

/* main.c: shared print helpers (ConOut + COM1 serial). */
void bl_print(const char *s);
void bl_print_hex(UINT64 v);
void bl_fatal(const char *msg);

/* gop.c */
EFI_STATUS gop_setup(struct boot_ctx *ctx);

/* acpi.c */
void acpi_find(struct boot_ctx *ctx);

/* fs.c: load \kernel.elf from the ESP we were launched from. */
EFI_STATUS fs_load_kernel(struct boot_ctx *ctx);

/* elf.c: place PT_LOAD segments at p_paddr, report entry/geometry. */
EFI_STATUS elf_load(struct boot_ctx *ctx);

/* mmap.c: final GetMemoryMap, convert to axboot entries, fill bootinfo. */
EFI_STATUS mmap_build(struct boot_ctx *ctx, struct axboot_info *info);

/* boot.c: page tables + ExitBootServices retry + 64-bit handoff. */
EFI_STATUS boot_tables_build(struct boot_ctx *ctx);
void boot_handoff(UINT64 entry, UINT64 info_phys, UINT64 stack_top,
                  UINT64 cr3);

#endif /* AXBOOTLOADER_H */
