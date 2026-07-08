#ifndef AXIOME_ETHERNET_H
#define AXIOME_ETHERNET_H

#include <stdint.h>
#include "net_buf.h"
#include "netdev.h"

#define ETH_P_IP    0x0800
#define ETH_P_ARP   0x0806
#define ETH_HDR_LEN 14

struct eth_hdr {
    uint8_t  dst[6];
    uint8_t  src[6];
    uint16_t type;
} __attribute__((packed));

/* Send an Ethernet frame.  `type` is the ethertype in host order.
   `payload` is the mbuf chain containing the frame body (after the header). */
int ethernet_send(struct netdev *dev, const uint8_t *dst,
                  uint16_t type, struct mbuf *payload);

/* Called by netdev_rx_poll when a frame arrives.  Dispatches to ARP/IPv4. */
void ethernet_rx(struct netdev *dev, struct mbuf *m);

/* Register a handler for an ethertype. */
typedef void (*ethertype_fn)(struct netdev *dev, struct mbuf *m);
void ethertype_register(uint16_t type, ethertype_fn fn);

#endif
