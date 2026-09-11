/* GOP mode selection + framebuffer capture. */

#include "bootloader.h"

EFI_STATUS
gop_setup(struct boot_ctx *ctx)
{
    EFI_STATUS status;
    EFI_GUID guid = GraphicsOutputProtocol;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    EFI_HANDLE *handles = NULL;
    UINTN n_handles = 0;

    ctx->fb_valid = 0;

    status = ctx->bs->LocateHandleBuffer(ByProtocol, &guid, NULL,
                                         &n_handles, &handles);
    if (EFI_ERROR(status) || n_handles == 0 || !handles)
    {
        /* Fall back to the console's GOP handle. */
        status = ctx->bs->OpenProtocol(ctx->st->ConsoleOutHandle, &guid,
                                       (void **)&gop, ctx->image, NULL,
                                       EFI_OPEN_PROTOCOL_GET_PROTOCOL);
        if (EFI_ERROR(status) || !gop)
            return EFI_NOT_FOUND;
    }
    else
    {
        status = ctx->bs->OpenProtocol(handles[0], &guid, (void **)&gop,
                                       ctx->image, NULL,
                                       EFI_OPEN_PROTOCOL_GET_PROTOCOL);
        ctx->bs->FreePool(handles);
        if (EFI_ERROR(status) || !gop)
            return EFI_NOT_FOUND;
    }

    /* Prefer 1024x768x32 RGB; otherwise keep the firmware's current mode. */
    if (gop->Mode && gop->Mode->Info)
    {
        UINT32 want = gop->Mode->Mode;
        UINTN n_modes = gop->Mode->MaxMode;
        for (UINT32 m = 0; m < n_modes; m++)
        {
            UINTN sz = 0;
            EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi = NULL;
            if (EFI_ERROR(gop->QueryMode(gop, m, &sz, &mi)))
                continue;
            if (mi->HorizontalResolution == 1024 &&
                mi->VerticalResolution == 768 &&
                mi->PixelFormat ==
                    PixelRedGreenBlueReserved8BitPerColor)
            {
                want = m;
                break;
            }
        }
        gop->SetMode(gop, want);
    }

    if (!gop->Mode || !gop->Mode->Info)
        return EFI_NOT_FOUND;
    if (gop->Mode->Info->PixelFormat !=
            PixelRedGreenBlueReserved8BitPerColor &&
        gop->Mode->Info->PixelFormat != PixelBlueGreenRedReserved8BitPerColor)
        return EFI_UNSUPPORTED;

    ctx->fb_addr = gop->Mode->FrameBufferBase;
    ctx->fb_width = gop->Mode->Info->HorizontalResolution;
    ctx->fb_height = gop->Mode->Info->VerticalResolution;
    ctx->fb_pitch = gop->Mode->Info->PixelsPerScanLine * 4;
    ctx->fb_bpp = 32;
    ctx->fb_pf =
        (gop->Mode->Info->PixelFormat ==
             PixelRedGreenBlueReserved8BitPerColor
             ? (UINT32)AXB_PF_RGB_8_8_8
             : (UINT32)AXB_PF_BGR_8_8_8);
    ctx->fb_valid = (ctx->fb_addr != 0 && ctx->fb_width > 0 &&
                     ctx->fb_height > 0);
    (void)gop;
    return ctx->fb_valid ? EFI_SUCCESS : EFI_NOT_FOUND;
}
