# Winsys plan: softpipe on /Devices/dri0 (Mesa 26.2.1)

Target: OpenGL (softpipe, no LLVM) for axiome userspace, presenting into
dumb buffers via `ports/mesa-axiome/axdri.{h,c}`. Vulkan/iris come later
(see `docs/mesa-port.md` Phase 3).

## 0. Prove the ABI first (no Mesa)

```sh
# axiome userspace toolchain, from the repo root:
$CC kernel/userspace/gfx_test.c ports/mesa-axiome/axdri.c -o /tmp/gfx_test
```

`kernel/userspace/gfx_test.c` (to be added with the libc mmap wrapper):
open `/Devices/dri0` → GET_MODE → CREATE 256x256 → MMAP → paint gradient →
PRESENT at (64,64) → DESTROY. If the gradient shows, the kernel side is
done and any Mesa failure is purely userspace.

## 1. New files inside ports/mesa (kept as a patch series, cf. §3)

```
src/gallium/winsys/axiome/meson.build
src/gallium/winsys/axiome/axiome_winsys.h   # pipe_winsys + alloc/mmap/present
src/gallium/winsys/axiome/axiome_winsys.c   # calls axdri_* (vendored below)
src/gallium/winsys/axiome/axiome_softpipe.c # softpipe_screen_create() glue
src/egl/drivers/axiome/egl_axiome.c         # EGL platform hooks (subset)
```

Copy `ports/mesa-axiome/axdri.{h,c}` into
`src/gallium/winsys/axiome/` so the winsys build is self-contained; the
sources here stay the canonical upstream of that copy.

Reference implementations to crib from (in the vendored tree):
`src/gallium/winsys/sw/` (wrapper), `src/gallium/winsys/kmsro/`
(dumb-buffer lifecycle on a render node), `src/egl/drivers/sw/` (surfaceless
driver skeleton).

## 2. Winsys behaviour

* `axiome_display_create(fd)` — `axdri_get_mode()` once, cache scanout size.
* dumb alloc — `axdri_create()` + `axdri_map()`; resource backing =
  `pipe_resource` with `PIPE_BIND_RENDER_TARGET | PIPE_BIND_DISPLAY_TARGET`.
* flush/frontbuffer — `axdri_present()` with the dirty rect; no vsync yet
  (present = blit + `gfx_present()`; tear is expected until page-flip lands).
* modifiers/prime/dmabuf — not supported; advertise a single X8R8G8B8 config.

## 3. Keeping the patch rebaseable

Do NOT commit edits inside `ports/mesa/`. Maintain them as
`ports/mesa-axiome/patches/*.patch` (git-format-patch against the pinned
tag) and re-apply after a pin bump:

```sh
git -C ports/mesa checkout mesa-XX.Y.Z
for p in ports/mesa-axiome/patches/*.patch; do
  patch -d ports/mesa -p1 < "$p"
done
```

## 4. Configure (host build for structure check, axiome cross later)

```sh
./ports/mesa-axiome/build-mesa.sh --help
```

Default configure is softpipe-only, LLVM/Gallium-extra/Vulkan off — the only
combination that can survive the axiome libc. New GL extensions beyond the
softpipe baseline are a Mesa-upstream concern, not ours.
