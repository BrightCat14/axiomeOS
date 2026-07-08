#ifndef AXIOME_NETDEV_H
#define AXIOME_NETDEV_H

#include <stdint.h>
#include "net_buf.h"
#include "net_util.h"

/*
 * Generic network device abstraction.
 * Every NIC driver (e1000, loopback, …) creates one of these and registers
 * it with netdev_register().  Protocol layers only interact through this
 * interface so they stay NIC-agnostic.
 */

struct netdev;

/* Transmit callback: the driver provides this.  Takes ownership of `m`. */
typedef int (*netdev_tx_fn)(struct netdev *dev, struct mbuf *m);

struct netdev {
    char        name[16];
    uint8_t     mac[ETH_ALEN];
    ip4_addr_t  ip;          /* IPv4 address (network order) */
    ip4_addr_t  netmask;
    ip4_addr_t  gateway;
    uint32_t    mtu;
    netdev_tx_fn tx;         /* driver-supplied TX entry point */
    void       *priv;        /* driver-private data (e.g. e1000_softc) */
    struct netdev *next;
};

/* Register a network device.  Returns 0 on success. */
void netdev_register(struct netdev *dev);

/* Find a device by name (e.g. "eth0").  Returns NULL if not found. */
struct netdev *netdev_find(const char *name);

/* Return first registered device (for iteration). */
struct netdev *netdev_first(void);

/* Called by the driver when a packet has been received.  Dispatches through
   the protocol stack (ethernet → ARP / IPv4 → …). */
void netdev_rx_poll(struct netdev *dev, struct mbuf *m);

/* Network stack initialisation (creates loopback, etc.). */
void net_init(void);

#endif
