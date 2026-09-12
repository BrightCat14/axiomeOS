#ifndef AXIOME_IPV4_H
#define AXIOME_IPV4_H

#include <stdint.h>
#include <stddef.h>
#include "net_buf.h"
#include "netdev.h"
#include "net_util.h"

#define IP_HDR_MIN_LEN 20
#define IP_PROTO_ICMP  1
#define IP_PROTO_TCP   6
#define IP_PROTO_UDP  17

struct ipv4_hdr {
    uint8_t  ver_ihl;       /* version (4 bits) + IHL (4 bits) */
    uint8_t  tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t flags_frag;    /* flags (3 bits) + fragment offset (13 bits) */
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    ip4_addr_t src;
    ip4_addr_t dst;
} __attribute__((packed));

/* Initialise IPv4: registers ethertype handler. */
void ipv4_init(void);

/* Send an IPv4 packet.  `proto` is the IP protocol number.
   The mbuf chain contains the payload (no IP header). */
int ipv4_send(struct netdev *dev, ip4_addr_t dst, uint8_t proto,
              struct mbuf *payload);

/* Send an IPv4 packet with explicit destination MAC and source address.
   Used for DHCP, which talks to 255.255.255.255 from 0.0.0.0 (no ARP, no
   routing table) and for testing.  Takes ownership of `payload`. */
int ipv4_send_direct(struct netdev *dev, const uint8_t *dst_mac,
                     ip4_addr_t src, ip4_addr_t dst, uint8_t proto,
                     struct mbuf *payload);

/* Receive callback — called from ethernet layer. */
void ipv4_rx(struct netdev *dev, struct mbuf *m);

/* Register a handler for an IP protocol number. */
typedef void (*ip_proto_fn)(struct netdev *dev, ip4_addr_t src,
                            ip4_addr_t dst, struct mbuf *m);
void ip_proto_register(uint8_t proto, ip_proto_fn fn);

/* Utility: compute IP header checksum. */
uint16_t ipv4_csum(const struct ipv4_hdr *hdr);

#endif
