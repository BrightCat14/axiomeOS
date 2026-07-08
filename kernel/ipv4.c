#include "ipv4.h"
#include "ethernet.h"
#include "arp.h"
#include "net_util.h"
#include "string.h"
#include "printk.h"

#define IP_MAX_HANDLERS 8

static struct {
    uint8_t      proto;
    ip_proto_fn  fn;
} g_ip_handlers[IP_MAX_HANDLERS];
static int g_ip_handler_count;

void ip_proto_register(uint8_t proto, ip_proto_fn fn)
{
    if (g_ip_handler_count >= IP_MAX_HANDLERS) return;
    g_ip_handlers[g_ip_handler_count].proto = proto;
    g_ip_handlers[g_ip_handler_count].fn   = fn;
    g_ip_handler_count++;
}

void ipv4_init(void)
{
    ethertype_register(ETH_P_IP, (ethertype_fn)ipv4_rx);
}

uint16_t ipv4_csum(const struct ipv4_hdr *hdr)
{
    uint16_t saved = hdr->checksum;
    ((struct ipv4_hdr *)hdr)->checksum = 0;
    uint16_t c = inet_csum(hdr, (hdr->ver_ihl & 0x0F) * 4);
    ((struct ipv4_hdr *)hdr)->checksum = saved;
    return c;
}

int ipv4_send(struct netdev *dev, ip4_addr_t dst, uint8_t proto,
              struct mbuf *payload)
{
    if (!dev || !payload) return -1;

    /* Determine destination MAC via ARP. */
    ip4_addr_t gw = dev->gateway;
    ip4_addr_t next_hop = (dst & dev->netmask) == (dev->ip & dev->netmask)
                          ? dst : gw;

    uint8_t dst_mac[ETH_ALEN];
    if (arp_resolve(dev, next_hop, dst_mac) != 0)
    {
        printk("IPv4: ARP failed for %d.%d.%d.%d\n",
               ip4_octet(next_hop, 0), ip4_octet(next_hop, 1),
               ip4_octet(next_hop, 2), ip4_octet(next_hop, 3));
        mbuf_free(payload);
        return -1;
    }

    /* Prepend IP header. */
    struct mbuf *hdr_m = mbuf_alloc0();
    if (!hdr_m) { mbuf_free(payload); return -1; }

    struct ipv4_hdr *iph = (struct ipv4_hdr *)(hdr_m->data + hdr_m->data_off);
    iph->ver_ihl = 0x45;            /* version 4, IHL 5 (20 bytes) */
    iph->tos = 0;
    uint16_t payload_len = (uint16_t)mbuf_total_len(payload);
    iph->total_len = htobe16(IP_HDR_MIN_LEN + payload_len);
    iph->id = 0;
    iph->flags_frag = 0;            /* no fragmentation */
    iph->ttl = 64;
    iph->protocol = proto;
    iph->checksum = 0;
    iph->src = dev->ip;
    iph->dst = dst;
    iph->checksum = ipv4_csum(iph);

    hdr_m->len = IP_HDR_MIN_LEN;
    hdr_m->next_seg = payload;

    return ethernet_send(dev, dst_mac, ETH_P_IP, hdr_m);
}

void ipv4_rx(struct netdev *dev, struct mbuf *m)
{
    if (m->len < IP_HDR_MIN_LEN) { mbuf_free(m); return; }

    struct ipv4_hdr *iph = (struct ipv4_hdr *)(m->data + m->data_off);

    int ihl = (iph->ver_ihl & 0x0F) * 4;
    if (iph->ver_ihl >> 4 != 4 || m->len < (size_t)ihl)
    {
        mbuf_free(m);
        return;
    }

    /* Verify checksum. */
    uint16_t csum = ipv4_csum(iph);
    if (csum != 0xFFFF) { mbuf_free(m); return; }

    /* Accept broadcast or unicast to us. */
    if (!ip4_is_broadcast(iph->dst) && iph->dst != dev->ip)
    {
        /* Not for us — drop (no IP forwarding). */
        mbuf_free(m);
        return;
    }

    ip4_addr_t src = iph->src;
    uint8_t proto = iph->protocol;

    /* Skip IP header. */
    m->data_off += ihl;
    m->len      -= ihl;

    /* Dispatch by protocol. */
    for (int i = 0; i < g_ip_handler_count; i++)
    {
        if (g_ip_handlers[i].proto == proto)
        {
            g_ip_handlers[i].fn(dev, src, iph->dst, m);
            return;
        }
    }
    mbuf_free(m);
}
