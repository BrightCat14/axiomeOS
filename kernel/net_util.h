#ifndef AXIOME_NET_UTIL_H
#define AXIOME_NET_UTIL_H

#include <stdint.h>
#include <stddef.h>

/* ---- byte order (x86 is little-endian, network is big-endian) ---- */
static inline uint16_t htobe16(uint16_t v) { return (v >> 8) | (v << 8); }
static inline uint32_t htobe32(uint32_t v)
{
    return ((v >> 24) & 0xFF) | ((v >> 8) & 0xFF00) |
           ((v << 8) & 0xFF0000) | ((v << 24) & 0xFF000000);
}
static inline uint16_t be16toh(uint16_t v) { return htobe16(v); }
static inline uint32_t be32toh(uint32_t v) { return htobe32(v); }

/* ---- ones-complement checksum ---- */
static inline uint16_t inet_csum(const void *data, size_t len)
{
    uint32_t sum = 0;
    const uint16_t *p = (const uint16_t *)data;
    while (len > 1) { sum += *p++; len -= 2; }
    if (len == 1) sum += *(const uint8_t *)p;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

/* ---- MAC address ---- */
#define ETH_ALEN 6
#define ETH_BROADCAST "\xff\xff\xff\xff\xff\xff"

static inline int mac_eq(const uint8_t *a, const uint8_t *b)
{
    for (int i = 0; i < ETH_ALEN; i++)
        if (a[i] != b[i]) return 0;
    return 1;
}

static inline int mac_is_broadcast(const uint8_t *m)
{
    for (int i = 0; i < ETH_ALEN; i++)
        if (m[i] != 0xFF) return 0;
    return 1;
}

static inline void mac_copy(uint8_t *dst, const uint8_t *src)
{
    for (int i = 0; i < ETH_ALEN; i++) dst[i] = src[i];
}

/* ---- IP helpers ---- */
typedef uint32_t ip4_addr_t; /* network byte order */

static inline ip4_addr_t ip4_make(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    return ((uint32_t)a) | ((uint32_t)b << 8) |
           ((uint32_t)c << 16) | ((uint32_t)d << 24);
}

static inline int ip4_is_broadcast(ip4_addr_t addr)
{
    return addr == 0xFFFFFFFF;
}

static inline int ip4_is_multicast(ip4_addr_t addr)
{
    /* ip4_make puts the first octet in the low byte, so 224.0.0.0/4 is
       detected from octet 0 (0xE0..0xEF). */
    return (addr & 0x000000F0u) == 0x000000E0u;
}

static inline uint8_t ip4_octet(ip4_addr_t addr, int i)
{
    return (uint8_t)(addr >> (i * 8));
}

#endif
