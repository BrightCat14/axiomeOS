#ifndef AXIOME_GFX_DISPLAY_H
#define AXIOME_GFX_DISPLAY_H

#include <stdint.h>
#include <stddef.h>
#include "gfx_types.h"

namespace gfx {

/* Abstract display backend. One subclass per hardware path:
     GopDisplay    - UEFI GOP linear framebuffer (always present on UEFI)
     IntelDisplay  - native Intel iGPU (i915/Xe), PCI 8086:display class
     BochsDisplay  - QEMU std/VGA (Bochs VBE 0x1234:0x1111) + virtio-gpu stub
   The manager picks the highest-priority backend whose probe() succeeds;
   today that is GOP unless Intel/Bochs bring real modesetting online.
   All methods must be callable before the scheduler starts (no sleeping). */
class IDisplay {
public:
    virtual const char *name() const = 0;
    /* Priority for auto-selection: higher wins. GOP=10, Bochs=20,
       Intel native=30 (when fully modeset-capable). */
    virtual int priority() const = 0;
    /* Detect hardware. Return true when this backend can drive the boot
       framebuffer / PCI device. Must not touch the screen. */
    virtual bool probe() = 0;
    /* Take over the display with `mode`. For GOP this just adopts the
       firmware mode; native drivers do full modeset here. */
    virtual bool init(const GfxMode &mode) = 0;
    virtual GfxMode mode() const = 0;
    virtual GfxCaps caps() const = 0;

    /* 2D primitives every backend must provide (CPU fallback is fine). */
    virtual void clear(uint32_t rgb) = 0;
    virtual void fill_rect(const GfxRect &r, uint32_t rgb) = 0;
    virtual void blit(const GfxRect &dst, const uint32_t *src,
                      uint32_t src_pitch_px) = 0;
    /* Push staged pixels to the scanout surface (GOP: WC flush + sfence). */
    virtual void present() = 0;

    /* Linear CPU mapping for software rasterizers (Mesa softpipe/llvmpipe
       target) plus geometry for dumb-buffer allocation. */
    virtual void *cpu_base() = 0;
    virtual uint32_t fb_width() const = 0;
    virtual uint32_t fb_height() const = 0;
    virtual uint32_t fb_pitch() const = 0;
    virtual PixelFormat fb_format() const = 0;

    virtual ~IDisplay() = default;
};

} /* namespace gfx */

#endif
