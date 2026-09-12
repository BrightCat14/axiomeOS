#ifndef AXIOME_GFX_INTEL_DISPLAY_H
#define AXIOME_GFX_INTEL_DISPLAY_H

#include "display.h"

namespace gfx {

/* Native Intel iGPU backend (i915 / Xe), staged bring-up:
   Stage 1 (this file): PCI probe only. Detects vendor 0x8086 + display
     class (0x03), records BAR0/device ID, reports caps with has_3d=0 and
     refuses modeset so the manager keeps GOP active. Safe on hardware
     without Intel graphics (probe returns false).
   Stage 2 (planned): GTT init, display-engine modeset, command submission
     for fill/blit, then has_hw_* flips on and priority beats GOP.
   Keeping the stub in-tree now means Mesa and the manager already speak
   to an Intel object; enabling acceleration later touches only this file. */
class IntelDisplay : public IDisplay {
public:
    IntelDisplay();
    const char *name() const override { return "intel"; }
    int priority() const override { return 30; }
    bool probe() override;
    bool init(const GfxMode &mode) override;
    GfxMode mode() const override { return shadow_; }
    GfxCaps caps() const override;
    void clear(uint32_t rgb) override;
    void fill_rect(const GfxRect &r, uint32_t rgb) override;
    void blit(const GfxRect &dst, const uint32_t *src,
              uint32_t src_pitch_px) override;
    void present() override;
    void *cpu_base() override { return nullptr; }
    uint32_t fb_width() const override { return shadow_.width; }
    uint32_t fb_height() const override { return shadow_.height; }
    uint32_t fb_pitch() const override { return shadow_.pitch; }
    PixelFormat fb_format() const override { return shadow_.format; }

    /* Probe results for printk diagnostics. */
    uint16_t found_device() const { return device_id_; }
    uint8_t found_bus() const { return bus_; }

private:
    bool found_ = false;
    uint8_t bus_ = 0;
    uint8_t dev_ = 0;
    uint8_t func_ = 0;
    uint16_t device_id_ = 0;
    uint64_t bar0_ = 0;
    GfxMode shadow_;
};

IntelDisplay *intel_display_create(void);

} /* namespace gfx */

#endif
