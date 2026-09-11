/* ACPI RSDP / SMBIOS discovery via the EFI configuration table. */

#include "bootloader.h"

static EFI_GUID g_acpi = ACPI_TABLE_GUID;
static EFI_GUID g_acpi20 = ACPI_20_TABLE_GUID;
static EFI_GUID g_smbios = SMBIOS_TABLE_GUID;
static EFI_GUID g_smbios3 = SMBIOS3_TABLE_GUID;

void
acpi_find(struct boot_ctx *ctx)
{
    EFI_CONFIGURATION_TABLE *ct = ctx->st->ConfigurationTable;
    UINTN n = ctx->st->NumberOfTableEntries;

    ctx->acpi_rsdp = 0;
    ctx->smbios = 0;

    for (UINTN i = 0; i < n; i++)
    {
        if (!CompareGuid(&ct[i].VendorGuid, &g_acpi20))
        {
            ctx->acpi_rsdp = (UINT64)(UINTN)ct[i].VendorTable;
            continue;
        }
        if (!CompareGuid(&ct[i].VendorGuid, &g_acpi))
        {
            /* Prefer ACPI 2.0; keep 1.0 only as a fallback. */
            if (ctx->acpi_rsdp == 0)
                ctx->acpi_rsdp = (UINT64)(UINTN)ct[i].VendorTable;
            continue;
        }
        if (!CompareGuid(&ct[i].VendorGuid, &g_smbios3))
        {
            ctx->smbios = (UINT64)(UINTN)ct[i].VendorTable;
            continue;
        }
        if (!CompareGuid(&ct[i].VendorGuid, &g_smbios))
        {
            if (ctx->smbios == 0)
                ctx->smbios = (UINT64)(UINTN)ct[i].VendorTable;
        }
    }
}
