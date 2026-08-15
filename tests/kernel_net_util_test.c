#include "test_util.h"

#include <stdint.h>

#include "net_util.h"

static int test_kernel_net_util(void)
{
    /* Byte order (x86 host is little-endian). */
    CHECK_EQ(htobe16(0x1234), 0x3412);
    CHECK_EQ(be16toh(htobe16(0x1234)), 0x1234);
    CHECK_EQ(htobe32(0x12345678u), 0x78563412u);
    CHECK_EQ(be32toh(htobe32(0x12345678u)), 0x12345678u);

    /* Internet checksum (ones-complement). RFC 1071 example:
       words 0001 f203 f4f5 f6f7 (as little-endian bytes) -> checksum 0x220d. */
    {
        const uint8_t data[] = {0x01, 0x00, 0x03, 0xf2, 0xf5, 0xf4, 0xf7, 0xf6};
        CHECK_EQ(inet_csum(data, sizeof(data)), 0x220d);
    }
    /* Zero-length -> ~0 == 0xffff. */
    CHECK_EQ(inet_csum(NULL, 0), 0xffff);
    /* Odd length folds the trailing byte in. */
    {
        const uint8_t data[] = {0x01, 0x02};
        CHECK_EQ(inet_csum(data, 1), (uint16_t)~0x0001);
        CHECK_EQ(inet_csum(data, 2), (uint16_t)~0x0201);
    }

    /* MAC helpers. */
    {
        const uint8_t a[ETH_ALEN] = {1, 2, 3, 4, 5, 6};
        const uint8_t b[ETH_ALEN] = {1, 2, 3, 4, 5, 6};
        const uint8_t c[ETH_ALEN] = {1, 2, 3, 4, 5, 7};
        uint8_t out[ETH_ALEN];

        CHECK(mac_eq(a, b));
        CHECK(!mac_eq(a, c));
        CHECK(!mac_is_broadcast(a));
        CHECK(mac_is_broadcast((const uint8_t *)ETH_BROADCAST));
        mac_copy(out, a);
        CHECK(mac_eq(out, a));
    }

    /* IP4 helpers. 10.1.168.192 in network (host) order. */
    {
        ip4_addr_t addr = ip4_make(192, 168, 1, 10);
        CHECK_EQ(ip4_octet(addr, 0), 192);
        CHECK_EQ(ip4_octet(addr, 1), 168);
        CHECK_EQ(ip4_octet(addr, 2), 1);
        CHECK_EQ(ip4_octet(addr, 3), 10);

        CHECK(!ip4_is_broadcast(addr));
        CHECK(ip4_is_broadcast(0xFFFFFFFFu));
        CHECK(!ip4_is_multicast(addr));
        CHECK(ip4_is_multicast(ip4_make(224, 0, 0, 1)));
        CHECK(ip4_is_multicast(ip4_make(239, 255, 255, 255)));
        CHECK(!ip4_is_multicast(ip4_make(223, 0, 0, 1)));
    }

    TEST_REPORT("kernel/net_util");
}

int main(void)
{
    return test_kernel_net_util();
}
