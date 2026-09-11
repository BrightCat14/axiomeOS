/* Final GetMemoryMap -> axboot mmap conversion + reservation fixups, then
   fill struct axboot_info at its fixed physical address. */

#include "bootloader.h"

static UINT32
efi_to_axb(UINT32 t)
{
    switch (t)
    {
    case EfiConventionalMemory:
    case EfiBootServicesCode:
    case EfiBootServicesData:
        /* Boot-services memory is usable once ExitBootServices runs. */
        return (UINT32)AXB_MEM_USABLE;
    case EfiACPIReclaimMemory:
        return (UINT32)AXB_MEM_ACPI_RECLAIM;
    case EfiACPIMemoryNVS:
        return (UINT32)AXB_MEM_ACPI_NVS;
    case EfiUnusableMemory:
        return (UINT32)AXB_MEM_BAD;
    default:
        /* Loader/runtime/reserved/MMIO/PalCode/... */
        return (UINT32)AXB_MEM_RESERVED;
    }
}

/* Carve a reserved hole [base, base+len) out of usable entries, splitting
   them in place. Non-usable entries already count as reserved for PMM, so
   only type-1 entries are touched. */
static void
reserve_range(struct axboot_mmap_entry *ents, UINT32 *count, UINT64 base,
              UINT64 len)
{
    UINT64 end = base + len;
    UINT32 n = *count;

    for (UINT32 i = 0; i < n; i++)
    {
        UINT64 eb = ents[i].base;
        UINT64 ee = eb + ents[i].length;

        if (ents[i].type != (UINT32)AXB_MEM_USABLE)
            continue;
        if (base >= ee || end <= eb)
            continue;

        if (base <= eb && end >= ee)
        {
            /* Fully covered: just retype. */
            ents[i].type = (UINT32)AXB_MEM_RESERVED;
        }
        else if (base <= eb)
        {
            /* Trim the head: [eb,end) becomes reserved. */
            ents[i].base = end;
            ents[i].length = ee - end;
            if (n + 1 > AXBOOT_MAX_MMAP)
                continue;
            for (UINT32 j = n; j > i; j--)
                ents[j] = ents[j - 1];
            ents[i].base = eb;
            ents[i].length = end - eb;
            ents[i].type = (UINT32)AXB_MEM_RESERVED;
            n++;
            i++; /* skip the reserved half we just inserted */
        }
        else if (end >= ee)
        {
            /* Trim the tail: [base,ee) becomes reserved. */
            ents[i].length = base - eb;
            if (n + 1 > AXBOOT_MAX_MMAP)
                continue;
            for (UINT32 j = n; j > i + 1; j--)
                ents[j] = ents[j - 1];
            ents[i + 1].base = base;
            ents[i + 1].length = ee - base;
            ents[i + 1].type = (UINT32)AXB_MEM_RESERVED;
            ents[i + 1].reserved = 0;
            n++;
            i++; /* skip the reserved half we just inserted */
        }
        else
        {
            /* Punch a hole: [base,end) reserved inside (eb,ee). */
            UINT64 tail_b = end;
            UINT64 tail_l = ee - end;
            ents[i].length = base - eb;
            if (n + 2 > AXBOOT_MAX_MMAP)
                continue;
            for (UINT32 j = n + 1; j > i + 1; j--)
                ents[j] = ents[j - 1];
            ents[i + 1].base = base;
            ents[i + 1].length = len;
            ents[i + 1].type = (UINT32)AXB_MEM_RESERVED;
            ents[i + 1].reserved = 0;
            ents[i + 2].base = tail_b;
            ents[i + 2].length = tail_l;
            ents[i + 2].type = (UINT32)AXB_MEM_USABLE;
            ents[i + 2].reserved = 0;
            n += 2;
            i += 2;
        }
    }
    *count = n;
}

EFI_STATUS
mmap_build(struct boot_ctx *ctx, struct axboot_info *info)
{
    EFI_STATUS status;
    UINTN map_size = 0;
    EFI_MEMORY_DESCRIPTOR *map = NULL;
    UINTN key = 0;
    UINTN desc_size = 0;
    UINT32 desc_ver = 0;
    struct axboot_mmap_entry tmp[AXBOOT_MAX_MMAP];
    UINT32 count = 0;

    /* Size query (expected to fail with BUFFER_TOO_SMALL). */
    status = ctx->bs->GetMemoryMap(&map_size, map, &key, &desc_size,
                                   &desc_ver);
    if (status != EFI_BUFFER_TOO_SMALL)
        return EFI_DEVICE_ERROR;

    /* Slack: the map itself plus room for descriptor growth so the key
       survives until ExitBootServices. */
    map_size += 8 * sizeof(EFI_MEMORY_DESCRIPTOR) + 4096;
    status = ctx->bs->AllocatePool(EfiLoaderData, map_size, (void **)&map);
    if (EFI_ERROR(status))
        return status;

    status = ctx->bs->GetMemoryMap(&map_size, map, &key, &desc_size,
                                   &desc_ver);
    if (EFI_ERROR(status))
    {
        ctx->bs->FreePool(map);
        return status;
    }

    ctx->memmap = map;
    ctx->memmap_size = map_size;
    ctx->desc_size = desc_size;
    ctx->desc_ver = desc_ver;
    ctx->memmap_key = key;
    ctx->n_desc = map_size / desc_size;

    /* Convert + coalesce adjacent same-type ranges. */
    for (UINTN i = 0; i < ctx->n_desc; i++)
    {
        EFI_MEMORY_DESCRIPTOR *d =
            (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)map + i * desc_size);
        UINT64 base = d->PhysicalStart;
        UINT64 len = d->NumberOfPages * 4096ULL;
        UINT32 type = efi_to_axb(d->Type);

        if (len == 0)
            continue;
        if (count > 0 && tmp[count - 1].type == type &&
            tmp[count - 1].base + tmp[count - 1].length == base)
        {
            tmp[count - 1].length += len;
            continue;
        }
        if (count >= AXBOOT_MAX_MMAP)
        {
            /* Merge into the last entry if types are compatible-ish;
               otherwise drop (should not happen on QEMU). */
            tmp[count - 1].length += len;
            if (tmp[count - 1].type != type)
                tmp[count - 1].type = (UINT32)AXB_MEM_RESERVED;
            continue;
        }
        tmp[count].base = base;
        tmp[count].length = len;
        tmp[count].type = type;
        tmp[count].reserved = 0;
        count++;
    }

    /* Explicit reservations (§4.3 of the boot architecture). */
    reserve_range(tmp, &count, ctx->kern_phys, ctx->kern_size);
    reserve_range(tmp, &count, ctx->info_phys,
                  ctx->info_pages * 4096ULL);
    reserve_range(tmp, &count, ctx->tables_phys,
                  ctx->tables_pages * 4096ULL);
    reserve_range(tmp, &count, ctx->stack_phys,
                  ctx->stack_pages * 4096ULL);
    if (ctx->fb_valid)
        reserve_range(tmp, &count, ctx->fb_addr,
                      (UINT64)ctx->fb_height * (UINT64)ctx->fb_pitch);

    /* Copy the array right after the header in the reserved region. */
    {
        struct axboot_mmap_entry *dst =
            (struct axboot_mmap_entry *)((UINT8 *)info + sizeof(*info));
        UINT64 max_addr = 0;

        for (UINT32 i = 0; i < count; i++)
        {
            dst[i] = tmp[i];
            if (tmp[i].type == (UINT32)AXB_MEM_USABLE)
            {
                UINT64 e = tmp[i].base + tmp[i].length;
                if (e > max_addr)
                    max_addr = e;
            }
        }

        info->magic = AXIOME_BOOT_MAGIC;
        info->version = AXBOOT_VERSION;
        info->flags = 0;
        info->mmap_count = count;
        info->_pad0 = 0;
        info->mmap = (struct axboot_mmap_entry *)(UINTN)
            (ctx->info_phys + sizeof(*info));

        info->fb_addr = ctx->fb_valid ? ctx->fb_addr : 0;
        info->fb_width = ctx->fb_valid ? ctx->fb_width : 0;
        info->fb_height = ctx->fb_valid ? ctx->fb_height : 0;
        info->fb_pitch = ctx->fb_valid ? ctx->fb_pitch : 0;
        info->fb_bpp = ctx->fb_valid ? ctx->fb_bpp : 0;
        info->fb_pixel_format = ctx->fb_valid ? ctx->fb_pf : 0;

        info->acpi_rsdp_addr = ctx->acpi_rsdp;
        info->smbios_addr = ctx->smbios;

        info->kernel_phys_load = ctx->kern_phys;
        info->kernel_virt_base = AXBOOT_KERNEL_VIRT_BASE;
        info->kernel_size = ctx->kern_size;

        info->boot_disk_bus = 0;
        info->boot_disk_drive = 0;
        info->boot_disk_part = 0;

        info->cmdline[0] = 0;
        (void)max_addr;
    }

    return EFI_SUCCESS;
}
