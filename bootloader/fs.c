/* Read \kernel.elf from the ESP, using only UEFI protocols
   (SimpleFileSystem). No storage drivers here.
   Strategy: enumerate every SimpleFileSystem handle and try \kernel.elf
   on each volume; the first hit wins. This avoids any dependence on
   LoadedImage/DevicePath plumbing. */

#include "bootloader.h"

static EFI_STATUS
try_volume(struct boot_ctx *ctx, EFI_HANDLE h)
{
    EFI_STATUS status;
    EFI_GUID fs_guid = FileSystemProtocol;
    EFI_GUID fi_guid = EFI_FILE_INFO_ID;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;
    EFI_FILE *root = NULL;
    EFI_FILE *file = NULL;
    EFI_FILE_INFO *fi = NULL;
    UINTN fi_size = 0;
    UINTN read_size;
    UINT8 *buf;

    status = ctx->bs->OpenProtocol(h, &fs_guid, (void **)&fs,
                                   ctx->image, NULL,
                                   EFI_OPEN_PROTOCOL_GET_PROTOCOL);
    if (EFI_ERROR(status) || !fs)
        return EFI_NOT_FOUND;

    status = fs->OpenVolume(fs, &root);
    if (EFI_ERROR(status) || !root)
        return EFI_NOT_FOUND;

    status = root->Open(root, &file, (CHAR16 *)L"\\kernel.elf",
                        EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status) || !file)
    {
        root->Close(root);
        return EFI_NOT_FOUND;
    }

    /* Query the file size first. */
    status = file->GetInfo(file, &fi_guid, &fi_size, NULL);
    if (status != EFI_BUFFER_TOO_SMALL)
    {
        file->Close(file);
        root->Close(root);
        return EFI_DEVICE_ERROR;
    }
    status = ctx->bs->AllocatePool(EfiLoaderData, fi_size, (void **)&fi);
    if (EFI_ERROR(status))
    {
        file->Close(file);
        root->Close(root);
        return status;
    }
    status = file->GetInfo(file, &fi_guid, &fi_size, fi);
    if (EFI_ERROR(status))
    {
        ctx->bs->FreePool(fi);
        file->Close(file);
        root->Close(root);
        return status;
    }

    read_size = (UINTN)fi->FileSize;
    ctx->bs->FreePool(fi);

    status = ctx->bs->AllocatePool(EfiLoaderData, read_size, (void **)&buf);
    if (EFI_ERROR(status))
    {
        file->Close(file);
        root->Close(root);
        return status;
    }

    status = file->Read(file, &read_size, buf);
    file->Close(file);
    root->Close(root);
    if (EFI_ERROR(status))
    {
        ctx->bs->FreePool(buf);
        return status;
    }

    ctx->file_data = buf;
    ctx->file_size = read_size;
    return EFI_SUCCESS;
}

EFI_STATUS
fs_load_kernel(struct boot_ctx *ctx)
{
    EFI_STATUS status;
    EFI_GUID fs_guid = FileSystemProtocol;
    EFI_HANDLE *handles = NULL;
    UINTN n_handles = 0;
    UINTN hits = 0;

    status = ctx->bs->LocateHandleBuffer(ByProtocol, &fs_guid, NULL,
                                         &n_handles, &handles);
    if (EFI_ERROR(status) || n_handles == 0 || !handles)
    {
        bl_print("axboot: no SimpleFileSystem handles\n");
        return EFI_NOT_FOUND;
    }

    for (UINTN i = 0; i < n_handles; i++)
    {
        if (!EFI_ERROR(try_volume(ctx, handles[i])))
        {
            hits++;
            break;
        }
    }
    ctx->bs->FreePool(handles);

    if (!hits)
    {
        bl_print("axboot: \\kernel.elf not found on any ESP\n");
        return EFI_NOT_FOUND;
    }
    return EFI_SUCCESS;
}
