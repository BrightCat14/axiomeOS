#!/bin/sh
# Configure the vendored Mesa for axiomeOS: softpipe-only, LLVM-free.
# Usage:
#   ./ports/mesa-axiome/build-mesa.sh [--cross] [--prefix DIR] [--build-dir DIR]
# --cross uses ports/mesa-axiome/meson-cross-axiome.ini (edit toolchain paths
# first). Without --cross this is a host structure check (still softpipe-only).
set -eu

CROSS=0
PREFIX="$PWD/build/mesa-prefix"
BUILDDIR="$PWD/build/mesa-build"
for a in "$@"; do
  case "$a" in
    --cross) CROSS=1 ;;
    --prefix=*) PREFIX="${a#--prefix=}" ;;
    --prefix) PREFIX="$2"; shift ;;
    --build-dir=*) BUILDDIR="${a#--build-dir=}" ;;
    --build-dir) BUILDDIR="$2"; shift ;;
    --help|-h)
      echo "usage: $0 [--cross] [--prefix DIR] [--build-dir DIR]";
      exit 0 ;;
    *) echo "unknown arg: $a" >&2; exit 1 ;;
  esac
done

MESON_CROSS=
if [ "$CROSS" -eq 1 ]; then
  MESON_CROSS="--cross-file $PWD/ports/mesa-axiome/meson-cross-axiome.ini"
fi

# shellcheck disable=SC2086
meson setup $MESON_CROSS \
  --prefix "$PREFIX" \
  --buildtype release \
  -Dplatforms=x11 \
  -Dgallium-drivers=softpipe \
  -Dvulkan-drivers= \
  -Dvideo-codecs= \
  -Dgallium-va=disabled \
  -Dgallium-vdpau=disabled \
  -Dgallium-xa=disabled \
  -Dgbm=disabled \
  -Dgles1=disabled \
  -Dgles2=enabled \
  -Dopengl=true \
  -Dosmesa=true \
  -Dllvm=disabled \
  -Dshared-llvm=disabled \
  -Dlibunwind=disabled \
  -Dlmsensors=disabled \
  -Dvalgrind=disabled \
  -Dzstd=disabled \
  "$BUILDDIR" ports/mesa
echo "configured: $BUILDDIR (prefix $PREFIX)"
echo "next: ninja -C $BUILDDIR"
