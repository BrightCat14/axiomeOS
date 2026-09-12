#include "tcp.h"
#include "ipv4.h"
#include "net_util.h"
#include "string.h"
#include "printk.h"
#include "sched.h"
#include "slab.h"
#include "clock.h"

#define TCP_EPHEMERAL_START 49152
#define TCP_EPHEMERAL_END   65535

static struct tcp_sock g_tcp_socks[TCP_SOCK_MAX];
static uint32_t g_tcp_isn;  /* initial sequence number */

/* ---- helpers ---- */
static uint32_t tcp_seq_add(uint32_t seq, uint32_t n) { return seq + n; }

/* a <= b in the 32-bit sequence space */
static int tcp_seq_leq(uint32_t a, uint32_t b)
{
    return (int32_t)(b - a) >= 0;
}

/* Block until `pred` turns true.  Busy-waits with sched_yield() (the same
   pattern tty.c uses) instead of suspending inside a syscall, so the main
   thread keeps polling the NIC.  `timeout_ms == (uint64_t)-1` waits forever. */
static int tcp_wait_for(int (*pred)(void *), void *arg, uint64_t timeout_ms)
{
    uint64_t deadline = 0;
    if (timeout_ms != (uint64_t)-1)
        deadline = clock_mono_ns() + timeout_ms * 1000000ULL;

    for (;;)
    {
        if (pred(arg)) return 0;
        if (timeout_ms != (uint64_t)-1 && clock_mono_ns() >= deadline)
            return -1;
        sched_yield();
    }
}

struct tcp_wait_ack {
    struct tcp_sock *ts;
    uint32_t target;   /* sequence number the peer must ACK past */
};

static int pred_tcp_established(void *arg)
{
    struct tcp_sock *ts = (struct tcp_sock *)arg;
    return ts->state == TCP_STATE_ESTABLISHED || ts->state == TCP_STATE_CLOSED;
}

static int pred_tcp_acked(void *arg)
{
    struct tcp_wait_ack *wa = (struct tcp_wait_ack *)arg;
    return tcp_seq_leq(wa->target, wa->ts->snd_una);
}

static int pred_tcp_accept(void *arg)
{
    struct tcp_sock *parent = (struct tcp_sock *)arg;
    for (int i = 0; i < TCP_SOCK_MAX; i++)
    {
        struct tcp_sock *s = &g_tcp_socks[i];
        if (s->in_use && s->parent == parent && s->state == TCP_STATE_ESTABLISHED)
            return 1;
    }
    return 0;
}

static int pred_tcp_recv(void *arg)
{
    struct tcp_sock *ts = (struct tcp_sock *)arg;
    return ts->rx_len > 0 || ts->state != TCP_STATE_ESTABLISHED;
}

static struct tcp_sock *tcp_alloc_sock(void)
{
    for (int i = 0; i < TCP_SOCK_MAX; i++)
    {
        if (!g_tcp_socks[i].in_use)
        {
            memset(&g_tcp_socks[i], 0, sizeof(struct tcp_sock));
            g_tcp_socks[i].in_use = 1;
            g_tcp_socks[i].state = TCP_STATE_CLOSED;
            g_tcp_socks[i].rcv_wnd = TCP_WND_SIZE;
            g_tcp_socks[i].snd_wnd = TCP_WND_SIZE;
            return &g_tcp_socks[i];
        }
    }
    return 0;
}

static uint16_t tcp_ephemeral_port(void)
{
    static uint16_t next = TCP_EPHEMERAL_START;
    for (int tries = 0; tries < TCP_EPHEMERAL_END - TCP_EPHEMERAL_START; tries++)
    {
        if (next >= TCP_EPHEMERAL_END)
            next = TCP_EPHEMERAL_START;
        next++;

        uint16_t p = next;
        int taken = 0;
        for (int i = 0; i < TCP_SOCK_MAX; i++)
        {
            if (g_tcp_socks[i].in_use && g_tcp_socks[i].local_port == p)
            {
                taken = 1;
                break;
            }
        }
        if (!taken) return p;
    }
    return 0;
}

/* Compute the TCP checksum (pseudo-header + header + payload).  The payload
   may be split across chained mbufs, so it is accumulated streaming rather
   than copied into a flat buffer. */
static uint16_t tcp_csum(ip4_addr_t src, ip4_addr_t dst,
                         struct mbuf *hdr_m, struct mbuf *data, size_t tcp_len)
{
    uint32_t sum = 0;
    sum = csum_acc(sum, &src, 4);
    sum = csum_acc(sum, &dst, 4);
    const uint8_t proto[2] = { 0, IP_PROTO_TCP };   /* zero + protocol */
    sum = csum_acc(sum, proto, 2);
    uint16_t len16 = htobe16((uint16_t)tcp_len);
    sum = csum_acc(sum, &len16, 2);

    struct tcp_hdr *th = (struct tcp_hdr *)(hdr_m->data + hdr_m->data_off);
    uint16_t saved = th->checksum;
    th->checksum = 0;
    sum = csum_acc(sum, th, TCP_HDR_LEN);
    th->checksum = saved;

    if (data && tcp_len > TCP_HDR_LEN)
        sum = mbuf_csum_acc(sum, data, 0, tcp_len - TCP_HDR_LEN);

    return csum_finish(sum);
}

/* Send a TCP segment (header, optionally chained to `data` payload). */
static int tcp_send_seg(struct tcp_sock *ts, uint8_t flags,
                        uint32_t seq, uint32_t ack, uint16_t wnd,
                        struct mbuf *data)
{
    struct netdev *dev = ts->dev;
    if (!dev) { if (data) mbuf_free(data); return -1; }

    struct mbuf *hdr_m = mbuf_alloc0();
    if (!hdr_m) { if (data) mbuf_free(data); return -1; }

    struct tcp_hdr *th = (struct tcp_hdr *)(hdr_m->data + hdr_m->data_off);
    th->sport = htobe16(ts->local_port);
    th->dport = htobe16(ts->remote_port);
    th->seq = htobe32(seq);
    th->ack = htobe32(ack);
    th->data_off = 0x50;  /* 5 * 4 = 20 bytes */
    th->flags = flags;
    th->window = htobe16(wnd);
    th->checksum = 0;
    th->urgent = 0;

    hdr_m->len = TCP_HDR_LEN;

    size_t data_len = data ? mbuf_total_len(data) : 0;
    if (data)
        hdr_m->next_seg = data;

    size_t tcp_len = TCP_HDR_LEN + data_len;
    th->checksum = tcp_csum(dev->ip, ts->remote_ip, hdr_m, data, tcp_len);

    return ipv4_send(dev, ts->remote_ip, IP_PROTO_TCP, hdr_m);
}

static void tcp_enter_state(struct tcp_sock *ts, int state)
{
    ts->state = state;
}

/* Drop all segments from the retransmit queue. */
static void tcp_flush_tx_queue(struct tcp_sock *ts)
{
    while (ts->tx_queue)
    {
        struct tcp_seg *seg = ts->tx_queue;
        ts->tx_queue = seg->next;
        if (seg->m) mbuf_free(seg->m);
        kfree(seg);
    }
    ts->tx_queue_len = 0;
}

/* ---- public API ---- */

void tcp_init(void)
{
    memset(g_tcp_socks, 0, sizeof(g_tcp_socks));
    g_tcp_isn = 0x12345678; /* would be random in production */
    ip_proto_register(IP_PROTO_TCP, (ip_proto_fn)tcp_rx);
}

int tcp_socket(void)
{
    struct tcp_sock *ts = tcp_alloc_sock();
    if (!ts) return -1;
    ts->local_port = 0;
    ts->dev = netdev_first();
    /* Return index. */
    for (int i = 0; i < TCP_SOCK_MAX; i++)
        if (&g_tcp_socks[i] == ts) return i;
    return -1;
}

int tcp_bind(int sock, uint16_t port)
{
    if (sock < 0 || sock >= TCP_SOCK_MAX) return -1;
    struct tcp_sock *ts = &g_tcp_socks[sock];
    if (!ts->in_use) return -1;
    ts->local_port = port;
    return 0;
}

int tcp_connect(int sock, ip4_addr_t dst_ip, uint16_t dst_port)
{
    if (sock < 0 || sock >= TCP_SOCK_MAX) return -1;
    struct tcp_sock *ts = &g_tcp_socks[sock];
    if (!ts->in_use) return -1;

    /* Route through the device that owns the destination subnet (so
       127.0.0.x goes out "lo", not the NIC). */
    struct netdev *dev = netdev_for_ip(dst_ip);
    if (!dev) return -1;
    ts->dev = dev;

    if (ts->local_port == 0)
        ts->local_port = tcp_ephemeral_port();
    if (ts->local_port == 0) return -1;

    ts->remote_ip = dst_ip;
    ts->remote_port = dst_port;
    ts->snd_nxt = g_tcp_isn;
    g_tcp_isn += 1000;
    ts->snd_una = ts->snd_nxt;
    tcp_enter_state(ts, TCP_STATE_SYN_SENT);

    /* Send SYN. */
    tcp_send_seg(ts, TCP_SYN, ts->snd_nxt, 0, TCP_WND_SIZE, 0);
    ts->snd_nxt = tcp_seq_add(ts->snd_nxt, 1);

    printk("TCP: connecting to %d.%d.%d.%d:%d (SYN sent, sport %u)\n",
           ip4_octet(dst_ip, 0), ip4_octet(dst_ip, 1),
           ip4_octet(dst_ip, 2), ip4_octet(dst_ip, 3), dst_port,
           ts->local_port);

    /* Block until ESTABLISHED or timeout. */
    tcp_wait_for(pred_tcp_established, ts, 10000);

    if (ts->state == TCP_STATE_ESTABLISHED)
        return 0;

    tcp_enter_state(ts, TCP_STATE_CLOSED);
    return -1;
}

int tcp_listen(int sock)
{
    if (sock < 0 || sock >= TCP_SOCK_MAX) return -1;
    struct tcp_sock *ts = &g_tcp_socks[sock];
    if (!ts->in_use) return -1;
    tcp_enter_state(ts, TCP_STATE_LISTEN);
    return 0;
}

int tcp_accept(int sock)
{
    if (sock < 0 || sock >= TCP_SOCK_MAX) return -1;
    struct tcp_sock *parent = &g_tcp_socks[sock];
    if (!parent->in_use || parent->state != TCP_STATE_LISTEN)
        return -1;

    /* Wait until a child reaches ESTABLISHED. */
    if (tcp_wait_for(pred_tcp_accept, parent, 10000) != 0)
        return -1;

    for (int i = 0; i < TCP_SOCK_MAX; i++)
    {
        struct tcp_sock *ts = &g_tcp_socks[i];
        if (ts->in_use && ts->parent == parent &&
            ts->state == TCP_STATE_ESTABLISHED)
        {
            return i;
        }
    }
    return -1;
}

int tcp_send(int sock, const void *buf, size_t len)
{
    if (sock < 0 || sock >= TCP_SOCK_MAX) return -1;
    struct tcp_sock *ts = &g_tcp_socks[sock];
    if (!ts->in_use || ts->state != TCP_STATE_ESTABLISHED)
        return -1;

    size_t sent = 0;
    const uint8_t *p = (const uint8_t *)buf;

    while (sent < len)
    {
        size_t chunk = len - sent;
        if (chunk > TCP_MSS) chunk = TCP_MSS;

        struct mbuf *m = mbuf_alloc0();
        if (!m) return (int)sent;
        memcpy(m->data + m->data_off, p + sent, chunk);
        m->len = chunk;

        /* Keep a copy for retransmission before the payload is consumed by
           the TX path. */
        struct tcp_seg *seg = (struct tcp_seg *)kmalloc(sizeof(*seg));
        if (seg)
        {
            seg->m = mbuf_clone(m);
            if (!seg->m) { kfree(seg); seg = 0; }
        }

        uint32_t seq = ts->snd_nxt;
        int r = tcp_send_seg(ts, TCP_ACK | TCP_PSH, seq, ts->rcv_nxt,
                             TCP_WND_SIZE, m);
        if (r < 0)
        {
            if (seg) { mbuf_free(seg->m); kfree(seg); }
            return (int)sent;
        }
        ts->snd_nxt = tcp_seq_add(ts->snd_nxt, chunk);

        if (seg)
        {
            seg->seq = seq;
            seg->len = chunk;
            seg->rexmit_time = 0;
            seg->rexmit_count = 0;
            seg->next = ts->tx_queue;
            ts->tx_queue = seg;
            ts->tx_queue_len++;
        }

        /* Wait for the peer to ACK up to seq+chunk. */
        struct tcp_wait_ack wa;
        wa.ts = ts;
        wa.target = tcp_seq_add(seq, chunk);
        tcp_wait_for(pred_tcp_acked, &wa, 10000);

        sent += chunk;
    }
    return (int)sent;
}

int tcp_recv(int sock, void *buf, size_t max)
{
    if (sock < 0 || sock >= TCP_SOCK_MAX) return -1;
    struct tcp_sock *ts = &g_tcp_socks[sock];
    if (!ts->in_use) return -1;

    tcp_wait_for(pred_tcp_recv, ts, (uint64_t)-1);

    if (ts->rx_len == 0) return 0; /* connection closed */

    size_t n = ts->rx_len;
    if (n > max) n = max;

    /* Copy out of rx_buf chain. */
    size_t copied = mbuf_copyout(buf, n, ts->rx_buf, 0);

    /* Consume from rx_buf. */
    size_t consumed = copied;
    while (consumed > 0 && ts->rx_buf)
    {
        struct mbuf *m = ts->rx_buf;
        if (consumed >= m->len)
        {
            consumed -= m->len;
            ts->rx_buf = m->next_seg;
            mbuf_free(m);
        }
        else
        {
            m->data_off += consumed;
            m->len -= consumed;
            consumed = 0;
        }
    }
    ts->rx_len -= copied;

    return (int)copied;
}

int tcp_close(int sock)
{
    if (sock < 0 || sock >= TCP_SOCK_MAX) return -1;
    struct tcp_sock *ts = &g_tcp_socks[sock];
    if (!ts->in_use) return -1;

    if (ts->state == TCP_STATE_ESTABLISHED)
    {
        /* Send FIN. */
        tcp_send_seg(ts, TCP_ACK | TCP_FIN, ts->snd_nxt, ts->rcv_nxt,
                     TCP_WND_SIZE, 0);
        ts->snd_nxt = tcp_seq_add(ts->snd_nxt, 1);
        tcp_enter_state(ts, TCP_STATE_FIN_WAIT_1);
    }

    /* Free rx buffer. */
    while (ts->rx_buf)
    {
        struct mbuf *m = ts->rx_buf;
        ts->rx_buf = m->next_seg;
        mbuf_free(m);
    }
    ts->rx_len = 0;

    tcp_flush_tx_queue(ts);

    if (ts->wait_recv)
    {
        sched_wake(ts->wait_recv);
        ts->wait_recv = 0;
    }

    ts->in_use = 0;
    return 0;
}

/* ---- RX: state machine ---- */

void tcp_rx(struct netdev *dev, ip4_addr_t src, ip4_addr_t dst, struct mbuf *m)
{
    (void)dst;

    if (m->len < TCP_HDR_LEN) { mbuf_free(m); return; }

    struct tcp_hdr *th = (struct tcp_hdr *)(m->data + m->data_off);
    uint16_t sport = be16toh(th->sport);
    uint16_t dport = be16toh(th->dport);
    uint32_t seq = be32toh(th->seq);
    uint32_t ack = be32toh(th->ack);
    uint8_t  flags = th->flags;
    uint16_t wnd = be16toh(th->window);
    int ihl = ((th->data_off >> 4) & 0x0F) * 4;

    size_t payload_len = m->len - ihl;
    m->data_off += ihl;
    m->len = payload_len;

    /* Find socket for this connection. */
    struct tcp_sock *ts = 0;
    for (int i = 0; i < TCP_SOCK_MAX; i++)
    {
        struct tcp_sock *s = &g_tcp_socks[i];
        if (s->in_use && s->local_port == dport &&
            s->remote_port == sport && s->remote_ip == src)
        {
            ts = s;
            break;
        }
    }

    /* If no match, check LISTEN sockets for passive open. */
    if (!ts)
    {
        for (int i = 0; i < TCP_SOCK_MAX; i++)
        {
            struct tcp_sock *s = &g_tcp_socks[i];
            if (s->in_use && s->state == TCP_STATE_LISTEN &&
                s->local_port == dport)
            {
                /* Create a child socket. */
                struct tcp_sock *child = tcp_alloc_sock();
                if (!child) { mbuf_free(m); return; }
                child->local_port = dport;
                child->remote_port = sport;
                child->remote_ip = src;
                child->dev = dev;
                child->parent = s;
                child->snd_nxt = g_tcp_isn;
                g_tcp_isn += 1000;
                /* The child starts in CLOSED, so drive the passive open
                   explicitly: enter SYN_RECEIVED and emit the SYN+ACK here
                   instead of falling into the state machine below (which
                   would see CLOSED and drop the SYN). */
                child->rcv_nxt = tcp_seq_add(seq, 1);
                tcp_enter_state(child, TCP_STATE_SYN_RECEIVED);
                tcp_send_seg(child, TCP_SYN | TCP_ACK, child->snd_nxt,
                             child->rcv_nxt, TCP_WND_SIZE, 0);
                child->snd_nxt = tcp_seq_add(child->snd_nxt, 1);
                mbuf_free(m);
                return;
            }
        }
    }

    if (!ts) { mbuf_free(m); return; }

    /* ---- State machine ---- */
    switch (ts->state)
    {
    case TCP_STATE_CLOSED:
        if (flags & TCP_RST) { mbuf_free(m); return; }
        break;

    case TCP_STATE_LISTEN:
        if (flags & TCP_SYN)
        {
            ts->rcv_nxt = tcp_seq_add(seq, 1);
            tcp_enter_state(ts, TCP_STATE_SYN_RECEIVED);
            tcp_send_seg(ts, TCP_SYN | TCP_ACK, ts->snd_nxt, ts->rcv_nxt,
                         TCP_WND_SIZE, 0);
            ts->snd_nxt = tcp_seq_add(ts->snd_nxt, 1);
        }
        break;

    case TCP_STATE_SYN_SENT:
        if (flags & TCP_SYN && flags & TCP_ACK)
        {
            ts->rcv_nxt = tcp_seq_add(seq, 1);
            ts->snd_una = ack;
            tcp_enter_state(ts, TCP_STATE_ESTABLISHED);
            tcp_send_seg(ts, TCP_ACK, ts->snd_nxt, ts->rcv_nxt,
                         TCP_WND_SIZE, 0);
            printk("TCP: connection established (client)\n");
        }
        else if (flags & TCP_RST)
        {
            tcp_enter_state(ts, TCP_STATE_CLOSED);
        }
        break;

    case TCP_STATE_SYN_RECEIVED:
        if (flags & TCP_ACK)
        {
            ts->snd_una = ack;
            ts->snd_wnd = wnd;
            tcp_enter_state(ts, TCP_STATE_ESTABLISHED);
            printk("TCP: connection established (server)\n");
        }
        break;

    case TCP_STATE_ESTABLISHED:
        /* Process ACK. */
        if (flags & TCP_ACK)
        {
            ts->snd_una = ack;
            ts->snd_wnd = wnd;

            /* Drop retransmit-queued segments the peer has now acked. */
            while (ts->tx_queue &&
                   tcp_seq_leq(tcp_seq_add(ts->tx_queue->seq, ts->tx_queue->len),
                               ack))
            {
                struct tcp_seg *seg = ts->tx_queue;
                ts->tx_queue = seg->next;
                if (seg->m) mbuf_free(seg->m);
                kfree(seg);
                if (ts->tx_queue_len > 0) ts->tx_queue_len--;
            }
        }

        /* Process incoming data. */
        if (payload_len > 0)
        {
            ts->rcv_nxt = tcp_seq_add(ts->rcv_nxt, payload_len);

            /* Append to rx buffer (chained via next_seg, matching
               mbuf_copyout). */
            if (ts->rx_len + payload_len <= 65536)
            {
                m->next = 0;
                m->next_seg = 0;
                if (!ts->rx_buf)
                    ts->rx_buf = m;
                else
                {
                    struct mbuf *tail = ts->rx_buf;
                    while (tail->next_seg) tail = tail->next_seg;
                    tail->next_seg = m;
                }
                ts->rx_len += payload_len;
                m = 0; /* don't free below */

                if (ts->wait_recv)
                {
                    sched_wake(ts->wait_recv);
                    ts->wait_recv = 0;
                }
            }

            /* Send ACK for the data. */
            tcp_send_seg(ts, TCP_ACK, ts->snd_nxt, ts->rcv_nxt,
                         TCP_WND_SIZE, 0);
        }

        /* Check for FIN. */
        if (flags & TCP_FIN)
        {
            ts->rcv_nxt = tcp_seq_add(ts->rcv_nxt, 1);
            tcp_send_seg(ts, TCP_ACK, ts->snd_nxt, ts->rcv_nxt,
                         TCP_WND_SIZE, 0);
            tcp_enter_state(ts, TCP_STATE_CLOSE_WAIT);
        }
        break;

    case TCP_STATE_FIN_WAIT_1:
        if (flags & TCP_ACK)
        {
            ts->snd_una = ack;
            if (flags & TCP_FIN)
            {
                ts->rcv_nxt = tcp_seq_add(ts->rcv_nxt, 1);
                tcp_send_seg(ts, TCP_ACK, ts->snd_nxt, ts->rcv_nxt,
                             TCP_WND_SIZE, 0);
                tcp_enter_state(ts, TCP_STATE_TIME_WAIT);
            }
            else
            {
                tcp_enter_state(ts, TCP_STATE_FIN_WAIT_2);
            }
        }
        break;

    case TCP_STATE_FIN_WAIT_2:
        if (flags & TCP_FIN)
        {
            ts->rcv_nxt = tcp_seq_add(ts->rcv_nxt, 1);
            tcp_send_seg(ts, TCP_ACK, ts->snd_nxt, ts->rcv_nxt,
                         TCP_WND_SIZE, 0);
            tcp_enter_state(ts, TCP_STATE_TIME_WAIT);
        }
        break;

    case TCP_STATE_TIME_WAIT:
        /* Just wait briefly and close. */
        tcp_enter_state(ts, TCP_STATE_CLOSED);
        break;

    case TCP_STATE_CLOSE_WAIT:
        /* Peer closed; we can still send. Just drop everything. */
        break;

    case TCP_STATE_LAST_ACK:
        if (flags & TCP_ACK)
        {
            ts->snd_una = ack;
            tcp_enter_state(ts, TCP_STATE_CLOSED);
        }
        break;

    case TCP_STATE_CLOSING:
        if (flags & TCP_ACK)
        {
            ts->snd_una = ack;
            tcp_enter_state(ts, TCP_STATE_TIME_WAIT);
        }
        break;
    }

    if (m) mbuf_free(m);
}

void tcp_tick(uint32_t ticks_ms)
{
    (void)ticks_ms;
    /* Retransmission check: scan all active connections. */
    for (int i = 0; i < TCP_SOCK_MAX; i++)
    {
        struct tcp_sock *ts = &g_tcp_socks[i];
        if (!ts->in_use) continue;

        /* SYN_SENT retransmit: if we're still in SYN_SENT after a while,
           resend the SYN. */
        if (ts->state == TCP_STATE_SYN_SENT)
        {
            /* Simple timeout: after ~2 seconds, give up. */
            static int syn_wait[TCP_SOCK_MAX];
            syn_wait[i]++;
            if (syn_wait[i] > 20)
            {
                tcp_send_seg(ts, TCP_SYN, ts->snd_nxt - 1, 0,
                             TCP_WND_SIZE, 0);
                if (syn_wait[i] > 60)
                {
                    tcp_enter_state(ts, TCP_STATE_CLOSED);
                    syn_wait[i] = 0;
                }
            }
        }
        else
        {
            static int syn_wait2[TCP_SOCK_MAX];
            syn_wait2[i] = 0;
        }

        /* Retransmit unACKed segments. */
        struct tcp_seg *seg = ts->tx_queue;
        while (seg)
        {
            if (seg->rexmit_count < TCP_RETX_MAX &&
                (int)(ticks_ms - seg->rexmit_time) > TCP_RETX_MS)
            {
                seg->rexmit_time = ticks_ms;
                seg->rexmit_count++;
                tcp_send_seg(ts, TCP_ACK | TCP_PSH, seg->seq, ts->rcv_nxt,
                             TCP_WND_SIZE, mbuf_clone(seg->m));
            }
            seg = seg->next;
        }
    }
}