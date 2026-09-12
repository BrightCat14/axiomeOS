#include "dhcp.h"
#include "udp.h"
#include "net_util.h"
#include "net_buf.h"
#include "sched.h"
#include "clock.h"
#include "string.h"
#include "printk.h"

#define DHCP_PORT_SERVER 67
#define DHCP_PORT_CLIENT 68

#define BOOTP_HDR_LEN 240        /* fixed header incl. magic cookie */
#define DHCP_MAGIC    0x63825363

#define DHCP_MSG_DISCOVER 1
#define DHCP_MSG_OFFER    2
#define DHCP_MSG_REQUEST  3
#define DHCP_MSG_ACK      5

#define OPT_PAD       0
#define OPT_SUBNET    1
#define OPT_ROUTER    3
#define OPT_DNS       6
#define OPT_DOMAIN    15
#define OPT_REQ_IP    50
#define OPT_LEASE     51
#define OPT_MSG_TYPE  53
#define OPT_SERVER_ID 54
#define OPT_PARAM_REQ 55
#define OPT_END       255

struct bootp {
    uint8_t   op;        /* 1 = BOOTREQUEST */
    uint8_t   htype;     /* 1 = ethernet */
    uint8_t   hlen;      /* 6 */
    uint8_t   hops;
    uint32_t  xid;
    uint16_t  secs;
    uint16_t  flags;     /* 0x8000 = request broadcast reply */
    ip4_addr_t ciaddr;
    ip4_addr_t yiaddr;
    ip4_addr_t siaddr;
    ip4_addr_t giaddr;
    uint8_t   chaddr[16];
    char      sname[64];
    char      file[128];
    uint32_t  magic;
} __attribute__((packed));

static struct netdev *g_dhcp_dev;
static int g_dhcp_running;
static uint32_t g_dhcp_xid;
static int sock_dhcp = -1;   /* UDP socket bound to port 68 */

static uint32_t dhcp_make_xid(struct netdev *dev)
{
    uint32_t x = (uint32_t)clock_mono_ns() ^ 0x9E3779B9;
    for (int i = 0; i < 6; i++)
        x ^= ((uint32_t)dev->mac[i]) << (i * 4);
    return x;
}

/* Build a BOOTP/DHCP message (BOOTP header + options) into `pkt`, which must
   be at least DHCP_MSG_SIZE bytes.  Returns total length. */
static size_t dhcp_build_msg(uint8_t *pkt, uint8_t type, ip4_addr_t req_ip,
                             ip4_addr_t server_id)
{
    struct bootp *bp = (struct bootp *)pkt;
    memset(bp, 0, BOOTP_HDR_LEN);
    bp->op = 1;
    bp->htype = 1;
    bp->hlen = 6;
    bp->hops = 0;
    bp->xid = g_dhcp_xid;
    bp->secs = 0;
    bp->flags = htobe16(0x8000);
    memcpy(bp->chaddr, g_dhcp_dev->mac, 6);
    bp->magic = htobe32(DHCP_MAGIC);

    uint8_t *o = pkt + BOOTP_HDR_LEN;
    *o++ = OPT_MSG_TYPE; *o++ = 1; *o++ = type;

    if (req_ip != 0)
    {
        *o++ = OPT_REQ_IP; *o++ = 4;
        memcpy(o, &req_ip, 4); o += 4;
    }
    if (server_id != 0)
    {
        *o++ = OPT_SERVER_ID; *o++ = 4;
        memcpy(o, &server_id, 4); o += 4;
    }
    if (type == DHCP_MSG_DISCOVER)
    {
        static const uint8_t plist[] = { OPT_SUBNET, OPT_ROUTER, OPT_DNS,
                                         OPT_DOMAIN };
        *o++ = OPT_PARAM_REQ; *o++ = sizeof(plist);
        memcpy(o, plist, sizeof(plist)); o += sizeof(plist);
    }
    *o++ = OPT_END;

    size_t n = BOOTP_HDR_LEN + (size_t)(o - (pkt + BOOTP_HDR_LEN));
    /* RFC 2131 §3.1.1: messages are minimum 300 octets. */
    while (n < 300) pkt[n++] = OPT_PAD;
    return n;
}

static int dhcp_send(uint8_t type, ip4_addr_t req_ip, ip4_addr_t server_id)
{
    struct netdev *dev = g_dhcp_dev;
    if (!dev) return -1;

    uint8_t pkt[320];
    size_t n = dhcp_build_msg(pkt, type, req_ip, server_id);

    ip4_addr_t src = 0;
    ip4_addr_t bcast = 0xFFFFFFFF;

    int r = udp_send_direct(sock_dhcp, dev, (const uint8_t *)ETH_BROADCAST,
                            src, bcast, DHCP_PORT_SERVER, pkt, n);
    return r < 0 ? -1 : 0;
}

/* Parse a DHCP reply.  Returns 0 on success (op==2, xid match) and fills
   output pointers (all optional). */
static int dhcp_parse_reply(const uint8_t *pkt, size_t len, ip4_addr_t *yiaddr,
                            uint32_t *lease, ip4_addr_t *netmask,
                            ip4_addr_t *router, ip4_addr_t *dns)
{
    if (len < BOOTP_HDR_LEN) return -1;
    const struct bootp *bp = (const struct bootp *)pkt;
    if (bp->op != 2 || bp->xid != g_dhcp_xid) return -1;
    if (be32toh(bp->magic) != DHCP_MAGIC) return -1;
    if (yiaddr) *yiaddr = bp->yiaddr;

    int msg_type = 0;
    const uint8_t *o = pkt + BOOTP_HDR_LEN;
    while (o < pkt + len)
    {
        uint8_t code = *o++;
        if (code == OPT_END) break;
        if (code == OPT_PAD) continue;
        if (o >= pkt + len) break;
        uint8_t l = *o++;
        if ((size_t)(o - pkt) + l > len) break;
        const uint8_t *v = o;
        o += l;

        switch (code)
        {
        case OPT_MSG_TYPE:
            if (l == 1) msg_type = v[0];
            break;
        case OPT_LEASE:
            if (l == 4 && lease) *lease = be32toh(*(const uint32_t *)v);
            break;
        case OPT_SUBNET:
            if (l == 4 && netmask) memcpy(netmask, v, 4);
            break;
        case OPT_ROUTER:
            if (l >= 4 && router) memcpy(router, v, 4);
            break;
        case OPT_DNS:
            if (l >= 4 && dns) memcpy(dns, v, 4);
            break;
        default:
            break;
        }
    }
    return msg_type;
}

/* Wait up to `ms` for a DHCP reply of the wanted type(s).  Returns msg_type,
   or 0 on timeout. */
static int dhcp_wait_reply(ip4_addr_t *yiaddr, ip4_addr_t *server_id,
                           ip4_addr_t *netmask, ip4_addr_t *router,
                           ip4_addr_t *dns, uint32_t *lease, uint64_t ms)
{
    uint64_t deadline = clock_mono_ns() + ms * 1000000ULL;
    int best = 0;

    while (clock_mono_ns() < deadline)
    {
        int rdy = udp_datagram_ready(sock_dhcp);
        if (rdy > 0)
        {
            uint8_t buf[400];
            ip4_addr_t from;
            uint16_t from_port;
            int n = udp_recvfrom(sock_dhcp, buf, sizeof(buf), &from, &from_port);
            if (n < BOOTP_HDR_LEN) continue;

            ip4_addr_t y = 0, sid = 0, nm = 0, rt = 0, dn = 0;
            uint32_t ls = 0;
            int t = dhcp_parse_reply(buf, (size_t)n, &y, &ls, &nm, &rt, &dn);
            if (t < 0) continue;          /* wrong server / xid */

            if (t == DHCP_MSG_ACK || t == DHCP_MSG_OFFER)
            {
                if (yiaddr) *yiaddr = y;
                if (server_id) *server_id = sid;
                if (netmask) *netmask = nm;
                if (router) *router = rt;
                if (dns) *dns = dn;
                if (lease) *lease = ls;
                best = t;
                if (t == DHCP_MSG_ACK) return t;
            }
        }
        sched_yield();
    }
    return best;
}

static void dhcp_thread(void *arg)
{
    struct netdev *dev = (struct netdev *)arg;
    g_dhcp_dev = dev;
    g_dhcp_xid = dhcp_make_xid(dev);

    sock_dhcp = udp_socket();
    if (sock_dhcp < 0 || udp_bind(sock_dhcp, DHCP_PORT_CLIENT) != 0)
        goto out;

    printk("DHCP: %s DISCOVER (xid %08x)\n", dev->name, g_dhcp_xid);
    int sr = dhcp_send(DHCP_MSG_DISCOVER, 0, 0);
    if (sr != 0)
        goto out;

    /* Wait for an offer (or a direct ACK). */
    ip4_addr_t offer_ip = 0, server_id = 0, netmask = 0, router = 0, dns = 0;
    uint32_t lease = 0;
    int t = dhcp_wait_reply(&offer_ip, &server_id, &netmask, &router, &dns,
                            &lease, 8000);
    if (t == 0)
    {
        printk("DHCP: %s timed out waiting for offer\n", dev->name);
        goto out;
    }

    printk("DHCP: %s offer %d.%d.%d.%d\n", dev->name,
           ip4_octet(offer_ip, 0), ip4_octet(offer_ip, 1),
           ip4_octet(offer_ip, 2), ip4_octet(offer_ip, 3));

    /* Acknowledge with a REQUEST; slirp replies with an ACK. */
    if (dhcp_send(DHCP_MSG_REQUEST, offer_ip, server_id) == 0)
    {
        ip4_addr_t ack_ip = offer_ip, ack_nm = netmask, ack_rt = router,
                   ack_dn = dns;
        int a = dhcp_wait_reply(&ack_ip, &server_id, &ack_nm, &ack_rt, &ack_dn,
                                &lease, 4000);
        if (a != 0)
        {
            offer_ip = ack_ip;
            netmask = ack_nm;
            router = ack_rt;
            dns = ack_dn;
        }
    }

    if (offer_ip == 0)
        goto out;

    dev->ip = offer_ip;
    if (netmask != 0) dev->netmask = netmask;
    if (router != 0) dev->gateway = router;
    if (dns != 0) dev->dns_server = dns;

    printk("DHCP: %s configured: ip %d.%d.%d.%d mask %d.%d.%d.%d gw %d.%d.%d.%d dns %d.%d.%d.%d lease %u s\n",
           dev->name,
           ip4_octet(dev->ip, 0), ip4_octet(dev->ip, 1),
           ip4_octet(dev->ip, 2), ip4_octet(dev->ip, 3),
           ip4_octet(dev->netmask, 0), ip4_octet(dev->netmask, 1),
           ip4_octet(dev->netmask, 2), ip4_octet(dev->netmask, 3),
           ip4_octet(dev->gateway, 0), ip4_octet(dev->gateway, 1),
           ip4_octet(dev->gateway, 2), ip4_octet(dev->gateway, 3),
           ip4_octet(dev->dns_server, 0), ip4_octet(dev->dns_server, 1),
           ip4_octet(dev->dns_server, 2), ip4_octet(dev->dns_server, 3),
           lease);

out:
    if (sock_dhcp >= 0)
    {
        udp_close(sock_dhcp);
        sock_dhcp = -1;
    }
    g_dhcp_running = 0;
    sched_exit(0);
}

int dhcp_start(struct netdev *dev)
{
    if (!dev || g_dhcp_running)
        return 0;
    g_dhcp_running = 1;
    g_dhcp_dev = dev;
    sock_dhcp = -1;
    sched_spawn(dhcp_thread, dev, "dhcp");
    return 0;
}