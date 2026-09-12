#include "icmp.h"
#include "ipv4.h"
#include "arp.h"
#include "net_util.h"
#include "string.h"
#include "printk.h"
#include "sched.h"
#include "clock.h"
#include "spinlock.h"

static volatile uint16_t g_ping_id;
static volatile uint16_t g_ping_seq;
static volatile int g_ping_hit;

/* Serialises concurrent echo transactions: the id/seq/hit triple is global
   state, so two overlapping pings would otherwise accept/clobber each
   other's replies.  Held across the whole request/wait (pings are short). */
static spinlock_t g_ping_lock = SPINLOCK_INIT;

void icmp_init(void)
{
    ip_proto_register(IP_PROTO_ICMP, (ip_proto_fn)icmp_rx);
}

void icmp_rx(struct netdev *dev, ip4_addr_t src, ip4_addr_t dst, struct mbuf *m)
{
    if (m->len < sizeof(struct icmp_hdr)) { mbuf_free(m); return; }

    struct icmp_hdr *icmph = (struct icmp_hdr *)(m->data + m->data_off);

    if (icmph->type == ICMP_ECHO_REPLY && g_ping_hit == 0)
    {
        /* Check whether this is the reply to our outstanding echo. */
        if (m->len >= sizeof(struct icmp_echo))
        {
            struct icmp_echo *e = (struct icmp_echo *)icmph;
            if (be16toh(e->id) == (uint16_t)g_ping_id &&
                be16toh(e->seq) == (uint16_t)g_ping_seq)
                g_ping_hit = 1;
        }
    }

    if (icmph->type == ICMP_ECHO_REQUEST)
    {
        printk("ICMP: echo request from %d.%d.%d.%d\n",
               ip4_octet(src, 0), ip4_octet(src, 1),
               ip4_octet(src, 2), ip4_octet(src, 3));

        /* Build echo reply: reuse the payload. */
        struct mbuf *reply = mbuf_clone(m);
        if (!reply) { mbuf_free(m); return; }

        struct icmp_hdr *rhdr = (struct icmp_hdr *)(reply->data + reply->data_off);
        rhdr->type = ICMP_ECHO_REPLY;
        rhdr->code = 0;
        rhdr->checksum = 0;
        rhdr->checksum = inet_csum(rhdr, reply->len);

        ipv4_send(dev, src, IP_PROTO_ICMP, reply);
    }
    mbuf_free(m);
}

int icmp_echo(ip4_addr_t dst, uint32_t timeout_ms, uint32_t *rtt_us)
{
    /* Claim the global ping state.  Yield while acquiring: a plain
       blocking spin could deadlock the holder on a single core, and the
       lock must stay IRQ-enabled (never spin_lock_irq) because the wait
       loop below yields with the lock held.  icmp_rx never takes this
       lock, so holding it across yields is deadlock-free. */
    while (!spin_trylock(&g_ping_lock))
        sched_yield();

    struct netdev *dev = netdev_for_ip(dst);
    if (!dev) { spin_unlock(&g_ping_lock); return -1; }

    struct mbuf *m = mbuf_alloc0();
    if (!m) { spin_unlock(&g_ping_lock); return -1; }

    uint16_t id = (uint16_t)(clock_mono_ns() & 0xFFFF);
    static uint16_t seq;
    seq++;

    struct icmp_echo *e = (struct icmp_echo *)(m->data + m->data_off);
    e->h.type = ICMP_ECHO_REQUEST;
    e->h.code = 0;
    e->h.checksum = 0;
    e->id = htobe16(id);
    e->seq = htobe16(seq);
    memset(e->pad, 0xAB, sizeof(e->pad));
    e->h.checksum = inet_csum(e, (uint16_t)sizeof(*e));
    m->len = sizeof(*e);

    g_ping_id = id;
    g_ping_seq = seq;
    g_ping_hit = 0;
    uint64_t sent_ns = clock_mono_ns();

    int r = ipv4_send(dev, dst, IP_PROTO_ICMP, m);
    if (r < 0) { spin_unlock(&g_ping_lock); return -1; }

    uint64_t deadline = sent_ns + (uint64_t)timeout_ms * 1000000ULL;
    while (!g_ping_hit && clock_mono_ns() < deadline)
        sched_yield();

    int ret = 0;
    if (!g_ping_hit)
        ret = -1;
    else if (rtt_us)
        *rtt_us = (uint32_t)((clock_mono_ns() - sent_ns) / 1000);
    spin_unlock(&g_ping_lock);
    return ret;
}