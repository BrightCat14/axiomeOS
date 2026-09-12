# Porting Mesa to axiomeOS

Full Mesa cannot be vendored in one patch (millions of lines, needs LLVM,
libdrm, wayland-style winsys). The viable path is: keep Mesa in userspace
as a ported library, give it the smallest kernel surface that satisfies a
software rasterizer first, then accelerate.

## Phase 0 — what this tree already provides

* `kernel/gfx/display.h` (`IDisplay`): mode, `cpu_base()`, `fill_rect`,
  `blit`, `present`, `caps`.
* `kernel/gfx/mesa_abi.h`: `AXDRI_*` ioctls (get-mode, dumb
  create/map/destroy, present) — the contract `/dev/dri/card0` will speak.
* `GopDisplay::cpu_base()`: WC or back-buffer staging pointer Mesa renders
  into; `present()` flushes with `sfence`.

## Phase 1 — kernel `/dev/dri/card0` (small, do here)

1. New `kernel/dri.c` char device implementing `mesa_abi.h` on top of
   `gfx::display_manager()`:
   * `GET_MODE` -> active `GfxMode`.
   * `DUMB_CREATE` -> `pmm_alloc_frames()` + `vmm_mmap_phys()` staging
     buffer, return handle/pitch/size.
   * `DUMB_MAP` -> offset for the existing `mmap` dev-op.
   * `PRESENT` -> `IDisplay::blit()` + `present()`.
2. New syscalls (or `ioctl` multiplex on the fd): reuse `device_register()`
   + `dev_ops::{read,write,mmap}` so Mesa opens `/dev/dri/card0` like any
   other device.
3. Test without Mesa: a `userspace/gfx_test.c` that creates a dumb buffer,
   paints a gradient, presents — proves the ABI before Mesa enters.

## Phase 2 — Mesa userspace port (build outside the kernel)

1. Cross-build Mesa with the axiome toolchain:
   `meson setup build -Dgallium-drivers=softpipe -Dvulkan-drivers= -Dosmesa=true -Dllvm=disabled`
   softpipe needs no LLVM/JIT and no libdrm.
2. Add `src/gallium/winsys/axiome/` (~300 lines): implement
   `pipe_winsys` + `pipe_screen` on `AXDRI_*` (dumb alloc, map via the
   `mmap` syscall, flush via `PRESENT`).
3. Add `src/egl/drivers/axiome/` or reuse surfaceless+EGL with
   `EGL_PLATFORM_SURFACELESS_MESA` backed by the winsys above.
4. Porting shims needed in `kernel/userspace/libc/`: pthreads (map to
   clone/`sched_spawn`), `mmap`/`munmap`, `clock_gettime`, minimal
   `libdrm`-free headers — point Mesa at `mesa_abi.h`, never at Linux UAPI.
5. Ship as `/System/Libraries/libGL.so` + `/bin/glxgears`-style demo;
   dynamic link against the existing `libc.sl` + `dynlink.c` loader.

## Phase 3 — accelerate

* Intel 3D only after `IntelDisplay` grows GTT + command submission: then
  extend `mesa_abi.h` with GEM execbuffer ioctls and write the `i915g`
  winsys instead of softpipe. Do not start here — validate the whole
  pipeline on softpipe first.

## What NOT to do

* Do not vendor Mesa source into this repo (use a ports/ manifest or
  submodule when Phase 2 starts).
* Do not put rasterization in the kernel: kernel stays dumb buffers +
  present; all GL runs in userspace.
* Do not write an Intel modeset + Mesa iris port simultaneously — land
  GOP + softpipe GL first, native display second, GPU submission last.
