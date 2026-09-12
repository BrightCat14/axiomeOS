/* Host test for the DRI transport encoding.
   Includes the REAL kernel/axdri_cmd.h and the REAL packing logic from
   ports/mesa-axiome/axdri.c. Built with -DAXDRI_HOST_TEST (stubs the libc
   transport) -DAXDRI_KERNEL_HEADERS (single struct layout, like the
   axiome userspace build). */
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "../kernel/axdri_cmd.h"
#include "../ports/mesa-axiome/axdri.c"

#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); return 1; } \
} while (0)

int main(void)
{
    /* Kernel header helpers. */
    CHECK(axdri_pitch_for(1024) == 4096u);
    CHECK(axdri_size_for(1024, 768) == (uint64_t)4096 * 768);
    CHECK(axdri_mmap_handle(axdri_mmap_off(7, 4096)) == 7u);
    CHECK(axdri_mmap_byte_off(axdri_mmap_off(7, 4096)) == 4096u);

    /* Encode helpers produce the exact wire structs dri.c parses. */
    {
        struct axdri_cmd c;
        memset(&c, 0, sizeof(c));
        axdri_encode_create(&c, 256, 128);
        CHECK(c.magic == AXDRI_MAGIC);
        CHECK(c.op == (uint32_t)AXDRI_DUMB_CREATE);
        CHECK(c.args[0] == 256 && c.args[1] == 128);
        CHECK(sizeof(c) == AXDRI_CMD_SIZE);
    }
    {
        struct axdri_cmd a, b;
        memset(&a, 0, sizeof(a));
        memset(&b, 0, sizeof(b));
        axdri_encode_present(&a, 3, 10, 20, 30, 40, 100, 50);
        /* Client packs identically (same layout, separate TU). */
        b.magic = AXDRI_MAGIC;
        b.op = (uint32_t)AXDRI_PRESENT;
        b.args[0] = 3;
        b.args[1] = (20u << 16) | 10u;
        b.args[2] = (40u << 16) | 30u;
        b.args[3] = (50u << 16) | 100u;
        CHECK(a.magic == b.magic && a.op == b.op);
        CHECK(a.args[0] == b.args[0] && a.args[1] == b.args[1]);
        CHECK(a.args[2] == b.args[2] && a.args[3] == b.args[3]);
        CHECK(a.args[4] == b.args[4] && a.args[5] == b.args[5]);
    }
    {
        struct axdri_cmd c;
        memset(&c, 0, sizeof(c));
        axdri_encode_destroy(&c, 9);
        CHECK(c.op == (uint32_t)AXDRI_DUMB_DESTROY && c.args[0] == 9);
    }
    /* ABI magic shared between kernel header and winsys client. */
    CHECK(AXDRI_MAGIC == 0x41584452u);
    printf("PASS dri/cmd-encode (5 checks)\n");
    return 0;
}
