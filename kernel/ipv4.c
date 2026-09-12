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

static int ipv4_build_and_send(struct netdev *dev, const uint8_t *dst_mac,
                               ip4_addr_t src, ip4_addr_t dst, uint8_t proto,
                               struct mbuf *payload)
{
    if (!dev || !payload || !dst_mac) return -1;

    /* Sanity: whole frame must fit a single mbuf chain. */
    size_t total = IP_HDR_MIN_LEN + mbuf_total_len(payload);
    if (total > MBUF_DATA_SIZE) { mbuf_free(payload); return -1; }

    /* Prepend IP header. */
    struct mbuf *hdr_m = mbuf_alloc0();
    if (!hdr_m) { mbuf_free(payload); return -1; }

    struct ipv4_hdr *iph = (struct ipv4_hdr *)(hdr_m->data + hdr_m->data_off);
    iph->ver_ihl = 0x45;            /* version 4, IHL 5 (20 bytes) */
    iph->tos = 0;
    iph->total_len = htobe16(IP_HDR_MIN_LEN + (uint16_t)mbuf_total_len(payload));
    iph->id = 0;
    iph->flags_frag = 0;            /* no fragmentation */
    iph->ttl = 64;
    iph->protocol = proto;
    iph->checksum = 0;
    iph->src = src;
    iph->dst = dst;
    iph->checksum = ipv4_csum(iph);

    hdr_m->len = IP_HDR_MIN_LEN;
    hdr_m->next_seg = payload;

    return ethernet_send(dev, dst_mac, ETH_P_IP, hdr_m);
}

int ipv4_send(struct netdev *dev, ip4_addr_t dst, uint8_t proto,
              struct mbuf *payload)
{
    if (!dev || !payload) return -1;

    uint8_t dst_mac[ETH_ALEN];

    if (ip4_is_broadcast(dst))
    {
        mac_copy(dst_mac, (const uint8_t *)ETH_BROADCAST);
        return ipv4_build_and_send(dev, dst_mac, dev->ip, dst, proto, payload);
    }

    /* Determine destination MAC via ARP. */
    ip4_addr_t gw = dev->gateway;
    ip4_addr_t next_hop = (dst & dev->netmask) == (dev->ip & dev->netmask)
                          ? dst : gw;

    if (arp_resolve(dev, next_hop, dst_mac) != 0)
    {
        printk("IPv4: ARP failed for %d.%d.%d.%d\n",
               ip4_octet(next_hop, 0), ip4_octet(next_hop, 1),
               ip4_octet(next_hop, 2), ip4_octet(next_hop, 3));
        mbuf_free(payload);
        return -1;
    }

    return ipv4_build_and_send(dev, dst_mac, dev->ip, dst, proto, payload);
}

int ipv4_send_direct(struct netdev *dev, const uint8_t *dst_mac,
                     ip4_addr_t src, ip4_addr_t dst, uint8_t proto,
                     struct mbuf *payload)
{
    return ipv4_build_and_send(dev, dst_mac, src, dst, proto, payload);
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

    /* Verify checksum: recompute over the header (with the checksum field
       zeroed) and compare against the stored value. */
    uint16_t stored_csum = iph->checksum;
    uint16_t csum = ipv4_csum(iph);
    if (csum != stored_csum) { mbuf_free(m); return; }

    /* Accept broadcast or unicast to us. */
    if (!ip4_is_broadcast(iph->dst) && iph->dst != dev->ip)
    {
        /* Not for us — drop (no IP forwarding). */
        mbuf_free(m);
        return;
    }

    ip4_addr_t src = iph->src;
    uint8_t proto = iph->protocol;

    /* Trim Ethernet padding: short frames are padded to 60 bytes, but
       m->len covers the whole frame.  The IP total_length field gives the
       true end of the IP packet — anything beyond it is padding (or CRC)
       and must not be mistaken for TCP/UDP payload. */
    uint16_t ip_total = be16toh(iph->total_len);
    if (ip_total < (uint16_t)ihl || (size_t)(ip_total - ihl) > m->len - (size_t)ihl)
    {
        mbuf_free(m);
        return;
    }

    /* Skip IP header. */
    m->data_off += ihl;
    m->len = (size_t)(ip_total - ihl);

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
