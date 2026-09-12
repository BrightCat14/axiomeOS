#ifndef AXIOME_GFX_GOP_DISPLAY_H
#define AXIOME_GFX_GOP_DISPLAY_H

#include "display.h"

namespace gfx {

/* UEFI GOP backend: wraps the firmware linear framebuffer that axboot
   captures (bootloader/gop.c) and kernel/framebuffer.c drives. CPU-only,
   WC-mapped, double-buffered via fb_* with a single full-screen dirty
   rect. This is the always-available fallback. */
class GopDisplay : public IDisplay {
public:
    GopDisplay();
    const char *name() const override { return "gop"; }
    int priority() const override { return 10; }
    bool probe() override;
    bool init(const GfxMode &mode) override;
    GfxMode mode() const override { return cur_; }
    GfxCaps caps() const override;
    void clear(uint32_t rgb) override;
    void fill_rect(const GfxRect &r, uint32_t rgb) override;
    void blit(const GfxRect &dst, const uint32_t *src,
              uint32_t src_pitch_px) override;
    void present() override;
    void *cpu_base() override;
    uint32_t fb_width() const override { return cur_.width; }
    uint32_t fb_height() const override { return cur_.height; }
    uint32_t fb_pitch() const override { return cur_.pitch; }
    PixelFormat fb_format() const override { return cur_.format; }

private:
    GfxMode cur_;
    bool ready_ = false;
};

GopDisplay *gop_display_create(void);

} /* namespace gfx */

#endif
