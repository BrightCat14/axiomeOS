# Porting Mesa to axiomeOS

Mesa is vendored as a pinned submodule (`ports/mesa`, tag mesa-26.2.1 —
see `ports/README.md` + `ports/mesa.version`); axiome glue lives in
`ports/mesa-axiome/` (client lib, winsys guide, Meson cross file). Never
commit edits inside `ports/mesa/` — keep them as patches under
`ports/mesa-axiome/patches/` (see `ports/mesa-axiome/WINSYS.md`).

The rule stays: keep Mesa in userspace as a ported library on the smallest
kernel surface that satisfies a software rasterizer first, then accelerate.

## Phase 0 — what this tree already provides

* `kernel/gfx/display.h` (`IDisplay`): mode, `cpu_base()`, `fill_rect`,
  `blit`, `present`, `caps`.
* `kernel/gfx/mesa_abi.h`: `AXDRI_*` ioctls (get-mode, dumb
  create/map/destroy, present) — the contract `/dev/dri/card0` will speak.
* `GopDisplay::cpu_base()`: WC or back-buffer staging pointer Mesa renders
  into; `present()` flushes with `sfence`.

## Phase 1 — kernel `/Devices/dri0` (landed)

`kernel/dri.c` (registered from `driver_init()` via `dri_init()`):
* `read()` -> `struct axdri_mode` of the active display (GET_MODE).
* `write()` -> one `struct axdri_cmd` per call (`kernel/axdri_cmd.h`):
  DUMB_CREATE (in-out reply: pitch/handle/size), PRESENT (blit + flush),
  DUMB_DESTROY. DUMB_MAP needs no command — the `mmap()` offset encodes
  the handle (`axdri_mmap_off()`).
* `mmap()` -> maps dumb-buffer frames into the caller (`MMU_USER|WRITE`).
* Transport test: `tests/axdri_cmd_test.c` (wire encoding, both sides).
* Proven in QEMU: `kernel/userspace/gfx_test.c` (shipped as `/Binaries/gfx_test`)
  runs GET_MODE -> CREATE -> MMAP (at the `AXDRI_MAP_HINT` window, PML4[4] —
  dri_mmap rejects supervisor PML4s) -> paint -> readback -> PRESENT ->
  DESTROY and prints `dri: PASS`. Note: headless run, on-screen pixels not
  visually confirmed yet.

## Phase 2 — Mesa userspace port (scaffolded, not yet built)

`ports/mesa-axiome/` holds the client (`axdri.{h,c}` against the axiome
libc: `open/read/write` + raw `SYS_MMAP`), the winsys recipe (`WINSYS.md`:
new `src/gallium/winsys/axiome/` softpipe target + EGL hooks, applied as
rebaseable patches), and the configure entry (`build-mesa.sh`,
`meson-cross-axiome.ini`):
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

* Do not commit edits inside `ports/mesa/` (patches go to
  `ports/mesa-axiome/patches/`).
* Do not put rasterization in the kernel: kernel stays dumb buffers +
  present; all GL runs in userspace.
* Do not write an Intel modeset + Mesa iris port simultaneously — land
  GOP + softpipe GL first, native display second, GPU submission last.
