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

/* Registered NIC poll functions (populated by modules such as e1000.kxt). */
#define NET_POLL_MAX 8
static void (*g_net_poll[NET_POLL_MAX])(void);
static int g_net_poll_n;

void netdev_register_poll(void (*fn)(void))
{
    if (!fn || g_net_poll_n >= NET_POLL_MAX)
    {
        printk("NET: poll registration full\n");
        return;
    }
    g_net_poll[g_net_poll_n++] = fn;
}

void netdev_unregister_poll(void (*fn)(void))
{
    for (int i = 0; i < g_net_poll_n; i++)
    {
        if (g_net_poll[i] == fn)
        {
            for (int j = i; j < g_net_poll_n - 1; j++)
                g_net_poll[j] = g_net_poll[j + 1];
            g_net_poll_n--;
            return;
        }
    }
}

void netdev_poll_all(void)
{
    for (int i = 0; i < g_net_poll_n; i++)
        g_net_poll[i]();
}

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

struct netdev *netdev_for_ip(ip4_addr_t dst)
{
    for (struct netdev *d = g_netdevs; d; d = d->next)
        if ((dst & d->netmask) == (d->ip & d->netmask))
            return d;
    return g_netdevs;
}

static int netdev_is_loopback(const struct netdev *d)
{
    return d->name[0] == 'l' && d->name[1] == 'o' && d->name[2] == 0;
}

struct netdev *netdev_up(void)
{
    for (struct netdev *d = g_netdevs; d; d = d->next)
        if (d->ip != 0 && !netdev_is_loopback(d))
            return d;
    return 0;
}

/* Protocol layers expect a single contiguous buffer.  Drivers that DMA whole
   frames deliver one mbuf, but TX-side chains (loopback, TCP splits) reach
   the RX path as chains, so coalesce them into one flat buffer first. */
static void netdev_rx_coalesce(struct mbuf **mp)
{
    struct mbuf *m = *mp;
    if (!m || !m->next_seg) return;

    size_t total = mbuf_total_len(m);
    if (total > MBUF_DATA_SIZE) { mbuf_free(m); *mp = 0; return; }

    struct mbuf *flat = mbuf_alloc0();
    if (!flat) { mbuf_free(m); *mp = 0; return; }

    mbuf_copyout(flat->data, total, m, 0);
    flat->len = total;
    flat->data_off = 0;
    flat->refcount = 1;
    flat->next_seg = 0;

    mbuf_free(m);
    *mp = flat;
}

/* Defined in ethernet.c — the entry point for received frames. */
extern void ethernet_rx(struct netdev *dev, struct mbuf *m);

void netdev_rx_poll(struct netdev *dev, struct mbuf *m)
{
    netdev_rx_coalesce(&m);
    if (m)
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
