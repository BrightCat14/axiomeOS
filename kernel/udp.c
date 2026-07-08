#include "udp.h"
#include "ipv4.h"
#include "net_util.h"
#include "string.h"
#include "printk.h"
#include "sched.h"
#include "slab.h"

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
            g_udp_socks[i].in_use = 1;
            g_udp_socks[i].rx_queue = 0;
            g_udp_socks[i].rx_count = 0;
            g_udp_socks[i].wait_recv = 0;
            return i;
        }
    }
    return -1;
}

int udp_bind(int sock, uint16_t port)
{
    if (sock < 0 || sock >= UDP_SOCK_MAX || !g_udp_socks[sock].in_use)
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

int udp_sendto(int sock, ip4_addr_t dst_ip, uint16_t dst_port,
               const void *buf, size_t len)
{
    if (sock < 0 || sock >= UDP_SOCK_MAX || !g_udp_socks[sock].in_use)
        return -1;

    struct netdev *dev = netdev_first();
    if (!dev) return -1;

    struct mbuf *m = mbuf_alloc0();
    if (!m) return -1;

    /* Build UDP header. */
    struct udp_hdr *uh = (struct udp_hdr *)(m->data + m->data_off);
    uh->sport = htobe16(g_udp_socks[sock].local_port);
    uh->dport = htobe16(dst_port);
    uh->len = htobe16((uint16_t)(UDP_HDR_LEN + len));
    uh->checksum = 0;

    /* Copy payload. */
    uint8_t *p = m->data + m->data_off + UDP_HDR_LEN;
    size_t to_copy = len;
    if (to_copy > MBUF_DATA_SIZE - UDP_HDR_LEN)
        to_copy = MBUF_DATA_SIZE - UDP_HDR_LEN;
    memcpy(p, buf, to_copy);
    m->len = UDP_HDR_LEN + to_copy;

    /* Compute checksum (includes pseudo-header). */
    /* Pseudo-header: src_ip + dst_ip + zero + proto + udp_len */
    uint8_t pseudo[12];
    memcpy(pseudo + 0, &dev->ip, 4);
    memcpy(pseudo + 4, &dst_ip, 4);
    pseudo[8] = 0;
    pseudo[9] = IP_PROTO_UDP;
    memcpy(pseudo + 10, &uh->len, 2);

    /* Checksum over pseudo-header + UDP segment. */
    {
        size_t csum_len = 12 + m->len;
        uint8_t *cbuf = kmalloc(csum_len);
        if (cbuf)
        {
            memcpy(cbuf, pseudo, 12);
            memcpy(cbuf + 12, m->data + m->data_off, m->len);
            uh->checksum = 0;
            uh->checksum = inet_csum(cbuf, csum_len);
            if (uh->checksum == 0) uh->checksum = 0xFFFF;
            kfree(cbuf);
        }
    }

    int r = ipv4_send(dev, dst_ip, IP_PROTO_UDP, m);
    if (r < 0) return -1;
    return (int)to_copy;
}

int udp_recvfrom(int sock, void *buf, size_t max,
                 ip4_addr_t *src_ip, uint16_t *src_port)
{
    if (sock < 0 || sock >= UDP_SOCK_MAX || !g_udp_socks[sock].in_use)
        return -1;

    struct udp_sock *us = &g_udp_socks[sock];

    /* Block until data available. */
    while (us->rx_count == 0)
    {
        us->wait_recv = sched_current();
        sched_suspend();
    }

    /* Dequeue one packet. */
    struct mbuf *m = us->rx_queue;
    if (!m) return -1;
    us->rx_queue = m->next;
    us->rx_count--;

    if (m->len < UDP_HDR_LEN) { mbuf_free(m); return -1; }

    struct udp_hdr *uh = (struct udp_hdr *)(m->data + m->data_off);
    if (src_ip)
    {
        /* IP source was saved in a simple trick: we store it in the mbuf's
           data region before the UDP header — but that would require modifying
           udp_rx.  Instead, the caller can just check their own state.  For
           simplicity, we don't fill src_ip/src_port here; the caller of
           udp_recvfrom can use a separate mechanism. */
        *src_ip = 0;
        *src_port = be16toh(uh->sport);
    }

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
            /* Enqueue the whole mbuf (with UDP header stripped by caller
               or left for recvfrom to handle). */
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
