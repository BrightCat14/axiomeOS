#ifndef AXDRI_H
#define AXDRI_H

/* Userspace client for /Devices/dri0 (see kernel/dri.c).
   Depends only on the axiome libc: open/read/write/close + raw SYS_MMAP.
   Mesa's future src/gallium/winsys/axiome/ should call these helpers
   instead of touching ioctls directly. */

#include <stddef.h>
#include <stdint.h>

#define AXDRI_PATH "/Devices/dri0"

/* Suggested mmap window for dumb buffers: PML4[4] (2TB) — under a USER
   PML4 entry, clear of the app image (PML4[2]), libc.sl (PML4[3]) and the
   supervisor entries. dri_mmap() rejects anything outside PML4[2..507]
   (minus reserves). */
#define AXDRI_MAP_HINT ((void *)0x200000000000ULL)

#ifdef AXDRI_KERNEL_HEADERS
/* Single source of truth with kernel/dri.c (build with -I<repo-root>). */
#include "kernel/gfx/mesa_abi.h"
#else
/* Self-contained fallback for the future Mesa winsys copy. */
struct axdri_mode {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t format;
};
#endif

struct axdri_buffer {
    uint32_t handle;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint64_t size;
    void *map; /* set by axdri_map(), NULL until then */
};

/* Returns fd or -1. */
int axdri_open(void);
/* Fills mode from a read() on the fd. 0 ok, -1 fail. */
int axdri_get_mode(int fd, struct axdri_mode *mode);
/* DUMB_CREATE via write(); fills buf fields. 0 ok, -1 fail. */
int axdri_create(int fd, struct axdri_buffer *buf);
/* SYS_MMAP the dumb buffer at `addr` (page-aligned, len = buf->size). */
int axdri_map(int fd, struct axdri_buffer *buf, void *addr);
/* PRESENT via write(). 0 ok, -1 fail. */
int axdri_present(int fd, const struct axdri_buffer *buf,
                  uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy,
                  uint32_t w, uint32_t h);
/* DUMB_DESTROY via write(). 0 ok, -1 fail. */
int axdri_destroy(int fd, const struct axdri_buffer *buf);

#endif
