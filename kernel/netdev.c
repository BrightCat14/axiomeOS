#include "netdev.h"
#include "net_buf.h"
#include "arp.h"
#include "ipv4.h"
#include "icmp.h"
#include "udp.h"
#include "tcp.h"
#include "loopback.h"
#include "printk.h"
#include "string.h"
#include "slab.h"

static struct netdev *g_netdevs;

void netdev_register(struct netdev *dev)
{
    dev->next = g_netdevs;
    g_netdevs = dev;
    printk("NET: registered %s (%x:%x:%x:%x:%x:%x)\n",
           dev->name,
           dev->mac[0], dev->mac[1], dev->mac[2],
           dev->mac[3], dev->mac[4], dev->mac[5]);
}

struct netdev *netdev_find(const char *name)
{
    for (struct netdev *d = g_netdevs; d; d = d->next)
        if (strcmp(d->name, name) == 0)
            return d;
    return 0;
}

struct netdev *netdev_first(void)
{
    return g_netdevs;
}

/* Defined in ethernet.c — the entry point for received frames. */
extern void ethernet_rx(struct netdev *dev, struct mbuf *m);

void netdev_rx_poll(struct netdev *dev, struct mbuf *m)
{
    ethernet_rx(dev, m);
}

void net_init(void)
{
    mbuf_init();
    arp_init();
    ipv4_init();
    icmp_init();
    udp_init();
    tcp_init();
    loopback_init();
    printk("NET: stack initialised\n");
}
