/* axboot UEFI bootloader entry: orchestrate GOP -> ACPI -> file -> ELF ->
   bootinfo/mmap -> page tables -> ExitBootServices -> 64-bit kernel jump.
   Uses UEFI protocols only for loading; no storage drivers of our own. */

#include "bootloader.h"

static struct boot_ctx g_ctx;

static inline void port_out8(UINT16 port, UINT8 val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline UINT8 port_in8(UINT16 port)
{
    UINT8 v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static void serial_init(void)
{
    port_out8(0x3F8 + 1, 0x00);
    port_out8(0x3F8 + 3, 0x80);
    port_out8(0x3F8 + 0, 0x03);
    port_out8(0x3F8 + 1, 0x00);
    port_out8(0x3F8 + 3, 0x03);
    port_out8(0x3F8 + 2, 0xC7);
    port_out8(0x3F8 + 4, 0x0B);
}

static void serial_putc(char c)
{
    if (c == '\n')
        serial_putc('\r');
    while (!(port_in8(0x3F8 + 5) & 0x20))
        ;
    port_out8(0x3F8, (UINT8)c);
}

/* ASCII -> serial COM1 only (no ConOut: keep pre-EBS output on the one
   path QEMU always wires to stdio). */
void bl_print(const char *s)
{
    for (const char *p = s; *p; p++)
        serial_putc(*p);
}

static const char HEXDIG[] = "0123456789ABCDEF";

void bl_print_hex(UINT64 v)
{
    char buf[19];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 16; i++)
        buf[2 + i] = HEXDIG[(v >> (60 - i * 4)) & 0xF];
    buf[18] = 0;
    bl_print(buf);
}

void bl_fatal(const char *msg)
{
    bl_print("axboot FATAL: ");
    bl_print(msg);
    bl_print("\n");
}

static void print_dec(UINT64 v)
{
    char buf[24];
    int i = 0;
    if (v == 0)
    {
        bl_print("0");
        return;
    }
    while (v > 0 && i < 20)
    {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    for (int j = i - 1; j >= 0; j--)
    {
        char c[2] = { buf[j], 0 };
        bl_print(c);
    }
}

EFI_STATUS
efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_STATUS status;
    struct boot_ctx *ctx = &g_ctx;
    struct axboot_info *info;
    EFI_PHYSICAL_ADDRESS addr;
    UINTN pages;

    InitializeLib(ImageHandle, SystemTable);
    serial_init();

    ctx->image = ImageHandle;
    ctx->st = SystemTable;
    ctx->bs = SystemTable->BootServices;

    /* Avoid a firmware watchdog reset while we load the kernel. */
    ctx->bs->SetWatchdogTimer(0, 0, 0, NULL);

    bl_print("axboot: axiomeOS UEFI bootloader (axboot v1)\n");

    /* 1. Framebuffer (GOP). Non-fatal: the kernel boots text-only without
       it, but normally OVMF always provides a GOP mode. */
    status = gop_setup(ctx);
    if (EFI_ERROR(status) || !ctx->fb_valid)
        bl_print("axboot: no GOP framebuffer, continuing text-only\n");
    else
    {
        bl_print("axboot: GOP ");
        print_dec(ctx->fb_width);
        bl_print("x");
        print_dec(ctx->fb_height);
        bl_print(" pitch=");
        print_dec(ctx->fb_pitch);
        bl_print(" fb=");
        bl_print_hex(ctx->fb_addr);
        bl_print("\n");
    }

    /* 2. ACPI RSDP / SMBIOS via the configuration table. */
    acpi_find(ctx);
    bl_print("axboot: ACPI RSDP=");
    bl_print_hex(ctx->acpi_rsdp);
    bl_print(" SMBIOS=");
    bl_print_hex(ctx->smbios);
    bl_print("\n");

    /* 3. Load \kernel.elf from our own ESP. */
    status = fs_load_kernel(ctx);
    if (EFI_ERROR(status))
    {
        bl_fatal("cannot load \\kernel.elf from ESP");
        return status;
    }
    bl_print("axboot: kernel.elf bytes=");
    print_dec(ctx->file_size);
    bl_print("\n");

    /* 4. Place PT_LOAD segments at p_paddr. */
    status = elf_load(ctx);
    if (EFI_ERROR(status))
    {
        bl_fatal("ELF load failed");
        return status;
    }
    /* The staging file buffer is no longer needed; free it before the
       final GetMemoryMap so the EBS key stays valid. */
    ctx->bs->FreePool(ctx->file_data);
    ctx->file_data = NULL;

    bl_print("axboot: kernel entry=");
    bl_print_hex(ctx->kern_entry);
    bl_print(" phys=");
    bl_print_hex(ctx->kern_phys);
    bl_print(" size=");
    bl_print_hex(ctx->kern_size);
    bl_print("\n");

    /* 5. Reserve the fixed bootinfo region. */
    addr = (EFI_PHYSICAL_ADDRESS)AXBOOT_INFO_PHYS;
    status = ctx->bs->AllocatePages(AllocateAddress, EfiLoaderData,
                                    BOOTINFO_PAGES, &addr);
    if (EFI_ERROR(status))
    {
        bl_print("axboot FATAL: cannot reserve bootinfo at ");
        bl_print_hex((UINT64)AXBOOT_INFO_PHYS);
        bl_print("\n");
        return status;
    }
    ctx->info_phys = (UINT64)addr;
    ctx->info_pages = BOOTINFO_PAGES;
    info = (struct axboot_info *)(UINTN)addr;

    /* 6. Loader page tables + handoff stack, allocated here so the final
       memory map already contains them. Page count covers PML4 + PDPT_lo
       + one PD per identity GiB + PDPT_hi + PD_kern. */
    {
        /* Identity coverage must reach the framebuffer if it sits high. */
        UINT64 hi = 0x100000000ULL;
        if (ctx->fb_valid)
        {
            UINT64 fb_end = ctx->fb_addr +
                (UINT64)ctx->fb_height * (UINT64)ctx->fb_pitch;
            if (fb_end > hi)
                hi = (fb_end + 0x3FFFFFFFULL) & ~0x3FFFFFFFULL;
        }
        pages = 3 + (hi >> 30) + 1; /* PML4 + PDPT_lo + 1GB PDs + PDPT_hi + PD_k */
        if (pages < 12)
            pages = 12;
        if (pages > 64)
            pages = 64;
    }
    addr = 0; /* AllocateAnyPages ignores the input, but be explicit. */
    status = ctx->bs->AllocatePages(AllocateAnyPages, EfiLoaderData,
                                    pages, &addr);
    if (EFI_ERROR(status))
    {
        bl_fatal("cannot allocate page tables");
        return status;
    }
    ctx->tables_phys = (UINT64)addr;
    ctx->tables_pages = pages;

    pages = HANDOFF_STACK_PAGES;
    addr = 0;
    status = ctx->bs->AllocatePages(AllocateAnyPages, EfiLoaderData,
                                    pages, &addr);
    if (EFI_ERROR(status))
    {
        bl_fatal("cannot allocate handoff stack");
        return status;
    }
    ctx->stack_phys = (UINT64)addr;
    ctx->stack_pages = pages;

    /* 7. Final memory map + axboot_info fill. */
    status = mmap_build(ctx, info);
    if (EFI_ERROR(status))
    {
        bl_fatal("memory map build failed");
        return status;
    }
    bl_print("axboot: mmap entries=");
    print_dec(info->mmap_count);
    bl_print(" bootinfo=");
    bl_print_hex((UINT64)(UINTN)info);
    bl_print("\n");

    /* 8. Page tables (pure stores into already-allocated pages; the EBS
       key stays valid). */
    status = boot_tables_build(ctx);
    if (EFI_ERROR(status))
    {
        bl_fatal("page table build failed");
        return status;
    }

    /* 9. Point of no return. Retry once if the map changed under us. */
    for (int attempt = 0; attempt < 3; attempt++)
    {
        status = ctx->bs->ExitBootServices(ImageHandle, ctx->memmap_key);
        if (!EFI_ERROR(status))
            break;
        /* Re-fetch the map and its key, re-fill sizes only (reservations
           are recomputed identically). */
        {
            UINTN sz = ctx->memmap_size;
            EFI_MEMORY_DESCRIPTOR *m = ctx->memmap;
            UINTN key = 0;
            UINTN dsz = 0;
            UINT32 dver = 0;
            EFI_STATUS s2 = ctx->bs->GetMemoryMap(&sz, m, &key, &dsz, &dver);
            if (s2 == EFI_BUFFER_TOO_SMALL)
            {
                /* Should not happen (we over-allocated), bail out. */
                bl_fatal("memory map grew during ExitBootServices");
                return EFI_BUFFER_TOO_SMALL;
            }
            ctx->memmap_key = key;
        }
    }
    if (EFI_ERROR(status))
    {
        bl_fatal("ExitBootServices failed");
        return status;
    }

    /* 10. 64-bit handoff. No boot-services calls beyond this point
       (serial port writes are direct hardware and still work). */
    boot_handoff(ctx->kern_entry, (UINT64)(UINTN)info,
                 ctx->stack_phys + ctx->stack_pages * 4096ULL,
                 ctx->tables_phys);

    /* Unreachable. */
    for (;;)
        __asm__ volatile("hlt");
}
