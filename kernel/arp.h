#ifndef AXIOME_ARP_H
#define AXIOME_ARP_H

#include <stdint.h>
#include "netdev.h"
#include "net_util.h"

#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY   2

struct arp_hdr {
    uint16_t hw_type;
    uint16_t proto_type;
    uint8_t  hw_len;
    uint8_t  proto_len;
    uint16_t opcode;
} __attribute__((packed));

struct arp_entry {
    ip4_addr_t  ip;
    uint8_t     mac[ETH_ALEN];
    uint8_t     valid;
    uint32_t    age;        /* ticks since last update */
};

/* Initialise ARP: registers ethertype handler, starts cache age timer. */
void arp_init(void);

/* Resolve `ip` to a MAC address.  Returns 0 and fills `mac_out` on success.
   May send an ARP request and block.  Returns -1 on timeout. */
int arp_resolve(struct netdev *dev, ip4_addr_t ip, uint8_t *mac_out);

/* Inject a static ARP entry (useful for the gateway). */
void arp_cache_insert(ip4_addr_t ip, const uint8_t *mac);

/* Age the ARP cache (called periodically). */
void arp_tick(void);

#endif
