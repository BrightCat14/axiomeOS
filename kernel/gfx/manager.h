#ifndef AXIOME_GFX_MANAGER_H
#define AXIOME_GFX_MANAGER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
#include "display.h"

namespace gfx {

/* Display manager: owns the backend registry and the active display.
   Backends are placement-new'd into static storage (no heap, no RTTI),
   registered once from gfx_init(), then the best probing backend wins.
   Native drivers that are still stubs probe true but fail init() until
   their modeset lands, so selection gracefully falls back to GOP. */
class DisplayManager {
public:
    static const int kMaxDisplays = 4;

    bool reg(IDisplay *d);
    void select_best();
    IDisplay *active() const { return active_; }
    const char *active_name() const;

    /* Convenience fan-out used by the C API and the console. */
    void clear(uint32_t rgb);
    void fill_rect(const GfxRect &r, uint32_t rgb);
    void present();

private:
    IDisplay *slots_[kMaxDisplays] = {};
    int count_ = 0;
    IDisplay *active_ = nullptr;
};

DisplayManager &display_manager(void);

} /* namespace gfx */

#endif /* __cplusplus */

#ifdef __cplusplus
extern "C" {
#endif

/* C ABI for the C console, syscalls and driver glue. */
void gfx_init(void);
int gfx_active(void);
const char *gfx_active_name(void);
void gfx_clear(uint32_t rgb);
void gfx_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                   uint32_t rgb);
void gfx_present(void);
int gfx_mode(uint32_t *w, uint32_t *h, uint32_t *pitch, uint32_t *bpp);
void *gfx_cpu_base(void);

#ifdef __cplusplus
}
#endif

#endif
