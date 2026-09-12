/* Host test for kernel/gfx/gfx_types.h clip_rect + pixel conversion.
   Compiles the real header (never a copy) against the host toolchain. */
#include <stdio.h>
#include <assert.h>

#include "../kernel/gfx/gfx_types.h"

#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); return 1; } \
} while (0)

int main(void)
{
    /* Fully inside. */
    {
        gfx::GfxRect r{10, 20, 100, 50};
        CHECK(gfx::clip_rect(r, 1024, 768));
        CHECK(r.x == 10 && r.y == 20 && r.w == 100 && r.h == 50);
    }
    /* Clipped right/bottom. */
    {
        gfx::GfxRect r{1000, 700, 100, 100};
        CHECK(gfx::clip_rect(r, 1024, 768));
        CHECK(r.w == 24 && r.h == 68);
    }
    /* Fully off-screen. */
    {
        gfx::GfxRect r{2000, 0, 10, 10};
        CHECK(!gfx::clip_rect(r, 1024, 768));
        CHECK(r.w == 0 && r.h == 0);
    }
    /* Zero-size rejected. */
    {
        gfx::GfxRect r{0, 0, 0, 10};
        CHECK(!gfx::clip_rect(r, 1024, 768));
    }
    /* Remainder-strip case from the GOP bug (600p, 16px font): a rect
       covering the 8px strip must survive clipping. */
    {
        gfx::GfxRect r{0, 592, 1024, 8};
        CHECK(gfx::clip_rect(r, 1024, 600));
        CHECK(r.h == 8);
    }
    /* Pixel order conversion. */
    {
        uint32_t rgb = 0x00AABBCC; /* R=AA G=BB B=CC */
        CHECK(gfx::canonical_to_native(rgb, gfx::PixelFormat::X8R8G8B8) == 0x00AABBCC);
        CHECK(gfx::canonical_to_native(rgb, gfx::PixelFormat::X8B8G8R8) == 0x00CCBBAA);
        CHECK(gfx::pixel_format_bpp(gfx::PixelFormat::X8R8G8B8) == 32);
        CHECK(gfx::pixel_format_bpp(gfx::PixelFormat::Unknown) == 0);
    }
    printf("PASS gfx/clip+pixel (6 checks)\n");
    return 0;
}
