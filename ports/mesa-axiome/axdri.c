/* axdri client: thin wrappers over read/write/mmap on /Devices/dri0.
   Command layout is shared with the kernel via kernel/axdri_cmd.h —
   host builds (tests) include this file with AXDRI_HOST_TEST to stub out
   the libc transport (see tests/axdri_cmd_test.c). */

#include "axdri.h"

#ifndef AXDRI_HOST_TEST
#include <syscall.h>
#else
/* Host test provides its own transport fakes; only the pure packing logic
   below is exercised. */
#endif

#ifdef AXDRI_KERNEL_HEADERS
/* Single source of truth: layout shared with kernel/dri.c. Build with
   -I<repo-root> -DAXDRI_KERNEL_HEADERS (axiome userspace rules do this). */
#include "kernel/axdri_cmd.h"
#define AXDRI_CMD_SIZE 32
#else
/* Self-contained fallback for the future Mesa winsys copy (which vendors
   axdri.{h,c} into src/gallium/winsys/axiome/ without kernel headers). */
#define AXDRI_MAGIC 0x41584452u

enum {
    AXDRI_GET_MODE = 0x01,
    AXDRI_DUMB_CREATE = 0x02,
    AXDRI_DUMB_MAP = 0x03,
    AXDRI_DUMB_DESTROY = 0x04,
    AXDRI_PRESENT = 0x05,
};

#define AXDRI_CMD_SIZE 32

struct axdri_cmd {
    uint32_t magic;
    uint32_t op;
    uint32_t args[6];
};

#endif /* AXDRI_KERNEL_HEADERS fallback */

#ifndef AXDRI_HOST_TEST

int axdri_open(void)
{
    return open(AXDRI_PATH, 2 /* O_RDWR */);
}

int axdri_get_mode(int fd, struct axdri_mode *mode)
{
    if (fd < 0 || !mode)
        return -1;
    long r = read(fd, mode, sizeof(*mode));
    return (r == (long)sizeof(*mode)) ? 0 : -1;
}

int axdri_create(int fd, struct axdri_buffer *buf)
{
    struct axdri_cmd c;
    if (fd < 0 || !buf || buf->width == 0 || buf->height == 0)
        return -1;
    c.magic = AXDRI_MAGIC;
    c.op = (uint32_t)AXDRI_DUMB_CREATE;
    c.args[0] = buf->width;
    c.args[1] = buf->height;
    c.args[2] = 0;
    c.args[3] = 0;
    c.args[4] = 0;
    c.args[5] = 0;
    long r = write(fd, &c, sizeof(c));
    if (r != (long)sizeof(c) || c.args[3] == 0)
        return -1;
    buf->pitch = c.args[2];
    buf->handle = c.args[3];
    buf->size = ((uint64_t)c.args[5] << 32) | c.args[4];
    buf->map = 0;
    return 0;
}

int axdri_map(int fd, struct axdri_buffer *buf, void *addr)
{
    uint64_t off;
    if (fd < 0 || !buf || buf->handle == 0 || !addr)
        return -1;
    off = ((uint64_t)buf->handle << 32); /* byte_off 0 */
    /* SYS_MMAP(fd, off, virt, len): mirrors libc fb_mmap(). */
    long r = syscall(55 /* SYS_MMAP */, (long)fd, (long)off, (long)addr,
                     (long)buf->size, 0, 0);
    if (r != 0)
        return -1;
    buf->map = addr;
    return 0;
}

int axdri_present(int fd, const struct axdri_buffer *buf,
                  uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy,
                  uint32_t w, uint32_t h)
{
    struct axdri_cmd c;
    if (fd < 0 || !buf || buf->handle == 0 || w == 0 || h == 0)
        return -1;
    c.magic = AXDRI_MAGIC;
    c.op = (uint32_t)AXDRI_PRESENT;
    c.args[0] = buf->handle;
    c.args[1] = (sy << 16) | (sx & 0xFFFFu);
    c.args[2] = (dy << 16) | (dx & 0xFFFFu);
    c.args[3] = (h << 16) | (w & 0xFFFFu);
    c.args[4] = 0;
    c.args[5] = 0;
    long r = write(fd, &c, sizeof(c));
    return (r == (long)sizeof(c)) ? 0 : -1;
}

int axdri_destroy(int fd, const struct axdri_buffer *buf)
{
    struct axdri_cmd c;
    if (fd < 0 || !buf || buf->handle == 0)
        return -1;
    c.magic = AXDRI_MAGIC;
    c.op = (uint32_t)AXDRI_DUMB_DESTROY;
    c.args[0] = buf->handle;
    c.args[1] = 0;
    c.args[2] = 0;
    c.args[3] = 0;
    c.args[4] = 0;
    c.args[5] = 0;
    long r = write(fd, &c, sizeof(c));
    return (r == (long)sizeof(c)) ? 0 : -1;
}

#endif /* !AXDRI_HOST_TEST */
