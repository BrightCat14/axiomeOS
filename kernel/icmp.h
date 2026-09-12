#ifndef AXIOME_ICMP_H
#define AXIOME_ICMP_H

#include <stdint.h>
#include "net_buf.h"
#include "netdev.h"
#include "net_util.h"

#define ICMP_ECHO_REQUEST 8
#define ICMP_ECHO_REPLY   0

struct icmp_hdr {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    /* The rest varies by type — for echo: id + seq */
} __attribute__((packed));

struct icmp_echo {
    struct icmp_hdr h;
    uint16_t id;
    uint16_t seq;
    uint8_t  pad[8];
} __attribute__((packed));

/* Initialise ICMP: registers IP protocol handler. */
void icmp_init(void);

/* Called when an ICMP packet arrives. */
void icmp_rx(struct netdev *dev, ip4_addr_t src, ip4_addr_t dst, struct mbuf *m);

/* Send an ICMP echo request to `dst` (network byte order) and block until a
   matching reply (or `timeout_ms` elapses).  Returns 0 on success and stores
   the round-trip time in microseconds in *rtt_us (may be NULL). */
int icmp_echo(ip4_addr_t dst, uint32_t timeout_ms, uint32_t *rtt_us);

#endif
