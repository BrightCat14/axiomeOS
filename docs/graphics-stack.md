# Graphics stack

## Layers

```
 userspace (Mesa softpipe/llvmpipe -> libAXGL -> /dev/dri/card0)
        |
 kernel/gfx (C++ abstraction, this directory)
   IDisplay -------------------- display_manager (select_best)
     |                                |
 GopDisplay (live)   IntelDisplay (stub)   BochsDisplay (stub)
     |
 kernel/framebuffer.c (text console + WC flush, dirty-rect)
     |
 UEFI GOP (bootloader/gop.c captures mode, mmu_map_framebuffer maps WC)
```

## Rules

* Portable code talks to `gfx::*` (`manager.h`) or the C ABI
  (`gfx_init/gfx_clear/gfx_fill_rect/gfx_present/gfx_mode/gfx_cpu_base`).
  Nothing outside `kernel/gfx/*_display.cpp` touches PCI BARs, GOP state,
  or `fb_*` internals directly.
* `IDisplay::probe()` is read-only. `init()` may modeset. A stub backend
  returns `probe()==true, init()==false` so the manager logs the hardware
  but keeps GOP as scanout owner.
* Priority decides the winner: GOP=10, Bochs=20, Intel=30. A fully
  modeset-capable native driver automatically wins once its `init()`
  starts returning true — no manager changes needed.

## Bringing Intel online (staged)

1. `IntelDisplay::probe()` already records bus/dev/func, device ID, BAR0.
2. Next: map BAR0 via `hal::mmio()`, init GTT, light the display pipe at
   the GOP mode (no mode enumeration yet), implement `fill_rect` with the
   blitter, flip `caps.has_hw_fill/blit` on.
3. Then: full modeset (EDID, PLL, pipe), `present()` becomes page-flip,
   `init()` returns true and GOP retires to fallback.

Same staging applies to `BochsDisplay` (VBE dispi ports / virtio-gpu
virtqueues) for QEMU `-vga std`.

## GOP bug that was fixed here

`framebuffer.c::shift_rows_up()` used `memcpy` on overlapping rows (UB —
gcc may copy backwards and smear glyphs), only scrolled
`(rows-1)*16` lines, cleared only the last text row, and dirtied
`(0,0,W,H-16)`. On modes whose height is not a multiple of 16 (e.g.
600p = 37*16+8) the 8px remainder strip was never scrolled, cleared, or
flushed — the "parts not cleared" bars. Fix: `memmove` line-by-line,
`fill_lines(scroll_h, height, bg)` covering text row + remainder, one
full-screen dirty rect, `set_pixel`/`fill_row` hardened for non-32bpp,
`fb_clear` honours `bg_color`.
