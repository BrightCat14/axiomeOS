#include "udp.h"
#include "ipv4.h"
#include "arp.h"
#include "net_util.h"
#include "string.h"
#include "printk.h"
#include "sched.h"
#include "slab.h"

#define UDP_EPHEMERAL_START 49152
#define UDP_EPHEMERAL_END   65535

static struct udp_sock g_udp_socks[UDP_SOCK_MAX];

void udp_init(void)
{
    memset(g_udp_socks, 0, sizeof(g_udp_socks));
    ip_proto_register(IP_PROTO_UDP, (ip_proto_fn)udp_rx);
}

int udp_socket(void)
{
    for (int i = 0; i < UDP_SOCK_MAX; i++)
    {
        if (!g_udp_socks[i].in_use)
        {
            memset(&g_udp_socks[i], 0, sizeof(g_udp_socks[i]));
            g_udp_socks[i].in_use = 1;
            return i;
        }
    }
    return -1;
}

/* Allocate a free dynamic-range port, skipping ports already in use. */
static uint16_t udp_ephemeral_port(void)
{
    static uint16_t next = UDP_EPHEMERAL_START;
    for (int tries = 0; tries < UDP_EPHEMERAL_END - UDP_EPHEMERAL_START; tries++)
    {
        if (next >= UDP_EPHEMERAL_END)
            next = UDP_EPHEMERAL_START;
        next++;

        uint16_t p = next;
        int taken = 0;
        for (int i = 0; i < UDP_SOCK_MAX; i++)
        {
            if (g_udp_socks[i].in_use && g_udp_socks[i].local_port == p)
            {
                taken = 1;
                break;
            }
        }
        if (!taken) return p;
    }
    return 0;
}

int udp_bind(int sock, uint16_t port)
{
    if (sock < 0 || sock >= UDP_SOCK_MAX || !g_udp_socks[sock].in_use)
        return -1;

    if (port == 0)
        port = udp_ephemeral_port();
    if (port == 0)
        return -1;

    /* Check for port conflict. */
    for (int i = 0; i < UDP_SOCK_MAX; i++)
    {
        if (i != sock && g_udp_socks[i].in_use &&
            g_udp_socks[i].local_port == port)
            return -1;
    }
    g_udp_socks[sock].local_port = port;
    return 0;
}

/* Shared segment builder: UDP header + checksum, then hand off to the IPv4
   layer with an explicit device, source and destination MAC. */
static int udp_build_and_send(int sock, struct netdev *dev,
                              const uint8_t *dst_mac, ip4_addr_t src_ip,
                              ip4_addr_t dst_ip, uint16_t dst_port,
                              const void *buf, size_t len)
{
    if (!dev || !buf || len == 0) return -1;

    uint16_t sport = g_udp_socks[sock].local_port;
    if (sport == 0)
    {
        sport = udp_ephemeral_port();
        if (sport == 0) return -1;
        g_udp_socks[sock].local_port = sport;
    }

    if (len > MBUF_DATA_SIZE - UDP_HDR_LEN)
        len = MBUF_DATA_SIZE - UDP_HDR_LEN;

    struct mbuf *m = mbuf_alloc0();
    if (!m) return -1;

    uint8_t *d = m->data + m->data_off;

    struct udp_hdr *uh = (struct udp_hdr *)d;
    uh->sport = htobe16(sport);
    uh->dport = htobe16(dst_port);
    uh->len = htobe16((uint16_t)(UDP_HDR_LEN + len));
    uh->checksum = 0;

    memcpy(d + UDP_HDR_LEN, (const uint8_t *)buf, len);
    m->len = UDP_HDR_LEN + len;

    /* RFC 768 checksum: pseudo-header + UDP header (checksum 0) + payload.
       Frames earlier used a flat copy buffer; streaming accumulation gives
       the identical result without a kmalloc. */
    uint32_t sum = 0;
    sum = csum_acc(sum, &src_ip, 4);
    sum = csum_acc(sum, &dst_ip, 4);
    const uint8_t proto[2] = { 0, IP_PROTO_UDP };   /* zero + protocol */
    sum = csum_acc(sum, proto, 2);
    sum = csum_acc(sum, &uh->len, 2);                /* UDP length field */
    sum = csum_acc(sum, d, (size_t)UDP_HDR_LEN + len);
    uh->checksum = csum_finish(sum);
    if (uh->checksum == 0) uh->checksum = 0xFFFF;    /* 0 means "no csum" */

    return ipv4_send_direct(dev, dst_mac, src_ip, dst_ip, IP_PROTO_UDP, m);
}

int udp_send_direct(int sock, struct netdev *dev, const uint8_t *dst_mac,
                    ip4_addr_t src_ip, ip4_addr_t dst_ip, uint16_t dst_port,
                    const void *buf, size_t len)
{
    if (sock < 0 || sock >= UDP_SOCK_MAX || !g_udp_socks[sock].in_use)
        return -1;
    return udp_build_and_send(sock, dev, dst_mac, src_ip, dst_ip, dst_port,
                              buf, len);
}

int udp_sendto(int sock, ip4_addr_t dst_ip, uint16_t dst_port,
               const void *buf, size_t len)
{
    if (sock < 0 || sock >= UDP_SOCK_MAX || !g_udp_socks[sock].in_use)
        return -1;

    struct netdev *dev = netdev_for_ip(dst_ip);
    if (!dev) return -1;

    /* Route via the device's own MAC for on-net destinations; broadcast is
       sent to the link broadcast address. */
    if (ip4_is_broadcast(dst_ip))
    {
        return udp_build_and_send(sock, dev, (const uint8_t *)ETH_BROADCAST,
                                  dev->ip, dst_ip, dst_port, buf, len);
    }

    uint8_t dst_mac[ETH_ALEN];
    if (arp_resolve(dev, dst_ip, dst_mac) != 0)
    {
        printk("UDP: ARP failed for %d.%d.%d.%d\n",
               ip4_octet(dst_ip, 0), ip4_octet(dst_ip, 1),
               ip4_octet(dst_ip, 2), ip4_octet(dst_ip, 3));
        return -1;
    }
    return udp_build_and_send(sock, dev, dst_mac, dev->ip, dst_ip, dst_port,
                              buf, len);
}

int udp_datagram_ready(int sock)
{
    if (sock < 0 || sock >= UDP_SOCK_MAX || !g_udp_socks[sock].in_use)
        return -1;
    return g_udp_socks[sock].rx_count > 0;
}

int udp_recvfrom(int sock, void *buf, size_t max,
                 ip4_addr_t *src_ip, uint16_t *src_port)
{
    if (sock < 0 || sock >= UDP_SOCK_MAX || !g_udp_socks[sock].in_use)
        return -1;

    struct udp_sock *us = &g_udp_socks[sock];

    /* Block until data available.  We busy-wait with sched_yield() rather
       than suspending inside a syscall (a known crash path, see tty.c), so
       the main thread keeps polling the NIC while we wait. */
    while (us->rx_count == 0)
    {
        if (!us->in_use) return -1;   /* socket closed while blocked */
        sched_yield();
    }

    /* Dequeue one packet. */
    struct mbuf *m = us->rx_queue;
    if (!m) return -1;
    us->rx_queue = m->next;
    us->rx_count--;

    if (m->len < UDP_HDR_LEN) { mbuf_free(m); return -1; }

    struct udp_hdr *uh = (struct udp_hdr *)(m->data + m->data_off);
    if (src_ip) *src_ip = (ip4_addr_t)m->rx_src_ip;
    if (src_port) *src_port = m->rx_src_port;

    m->data_off += UDP_HDR_LEN;
    m->len -= UDP_HDR_LEN;

    size_t n = m->len;
    if (n > max) n = max;
    memcpy(buf, m->data + m->data_off, n);
    mbuf_free(m);
    return (int)n;
}

void udp_close(int sock)
{
    if (sock < 0 || sock >= UDP_SOCK_MAX) return;
    struct udp_sock *us = &g_udp_socks[sock];
    /* Free any queued packets. */
    while (us->rx_queue)
    {
        struct mbuf *m = us->rx_queue;
        us->rx_queue = m->next;
        mbuf_free(m);
    }
    if (us->wait_recv)
    {
        sched_wake(us->wait_recv);
        us->wait_recv = 0;
    }
    us->in_use = 0;
}

void udp_rx(struct netdev *dev, ip4_addr_t src, ip4_addr_t dst, struct mbuf *m)
{
    (void)dev; (void)dst;

    if (m->len < UDP_HDR_LEN) { mbuf_free(m); return; }

    struct udp_hdr *uh = (struct udp_hdr *)(m->data + m->data_off);
    uint16_t dport = be16toh(uh->dport);

    /* Find a socket bound to this port. */
    for (int i = 0; i < UDP_SOCK_MAX; i++)
    {
        struct udp_sock *us = &g_udp_socks[i];
        if (us->in_use && us->local_port == dport)
        {
            /* Record sender metadata for udp_recvfrom(). */
            m->rx_src_ip = (uint32_t)src;
            m->rx_src_port = be16toh(uh->sport);
            m->next = 0;
            if (!us->rx_queue)
                us->rx_queue = m;
            else
            {
                struct mbuf *tail = us->rx_queue;
                while (tail->next) tail = tail->next;
                tail->next = m;
            }
            us->rx_count++;
            if (us->wait_recv)
            {
                sched_wake(us->wait_recv);
                us->wait_recv = 0;
            }
            return;
        }
    }
    mbuf_free(m);
}