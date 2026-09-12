# ports/mesa-axiome/ — axiomeOS glue for the vendored Mesa

This directory is *ours*; `ports/mesa/` is upstream (do not patch it in
place — rebase-proofing, see `ports/README.md`).

## Contents

* `axdri.h` / `axdri.c` — userspace client for `/Devices/dri0`, written
  against the axiome libc (`open/read/write` + raw `SYS_MMAP`). Compiles
  with the axiome userspace toolchain today, no Mesa required. This is the
  exact transport the future Gallium winsys will use.
* `WINSYS.md` — file-by-file guide for adding
  `src/gallium/winsys/axiome/` + EGL platform hooks to the vendored tree.
* `build-mesa.sh` — configures the vendored Mesa with Meson for a
  `softpipe`-only, LLVM-free userspace build.
* `meson-cross-axiome.ini` — Meson cross-file template for the axiome
  userspace ABI (adjust toolchain paths to your checkout).

## Try the ABI without Mesa

Build `axdri.c` into a `gfx_test` program (-gradient -> present): it proves
`/Devices/dri0` end-to-end before Mesa enters the picture. See `WINSYS.md`.
