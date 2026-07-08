#include "arp.h"
#include "ethernet.h"
#include "net_buf.h"
#include "net_util.h"
#include "string.h"
#include "printk.h"
#include "softirq.h"

static void arp_handle_rx(struct netdev *dev, struct mbuf *m);

#define ARP_CACHE_SIZE 16
#define ARP_TIMEOUT_TICKS 300   /* ~30 seconds at 10 Hz tick */

static struct arp_entry g_arp_cache[ARP_CACHE_SIZE];
static struct arp_entry g_arp_pending; /* single pending request */

void arp_init(void)
{
    memset(g_arp_cache, 0, sizeof(g_arp_cache));
    ethertype_register(ETH_P_ARP, (ethertype_fn)arp_handle_rx);
}

void arp_cache_insert(ip4_addr_t ip, const uint8_t *mac)
{
    /* Update existing entry. */
    for (int i = 0; i < ARP_CACHE_SIZE; i++)
    {
        if (g_arp_cache[i].valid && g_arp_cache[i].ip == ip)
        {
            mac_copy(g_arp_cache[i].mac, mac);
            g_arp_cache[i].age = 0;
            return;
        }
    }
    /* Insert new entry. */
    for (int i = 0; i < ARP_CACHE_SIZE; i++)
    {
        if (!g_arp_cache[i].valid)
        {
            g_arp_cache[i].ip = ip;
            mac_copy(g_arp_cache[i].mac, mac);
            g_arp_cache[i].valid = 1;
            g_arp_cache[i].age = 0;
            return;
        }
    }
    /* Cache full — overwrite oldest. */
    int oldest = 0;
    uint32_t max_age = g_arp_cache[0].age;
    for (int i = 1; i < ARP_CACHE_SIZE; i++)
    {
        if (g_arp_cache[i].age > max_age)
        {
            max_age = g_arp_cache[i].age;
            oldest = i;
        }
    }
    g_arp_cache[oldest].ip = ip;
    mac_copy(g_arp_cache[oldest].mac, mac);
    g_arp_cache[oldest].valid = 1;
    g_arp_cache[oldest].age = 0;
}

static int arp_lookup(ip4_addr_t ip, uint8_t *mac_out)
{
    for (int i = 0; i < ARP_CACHE_SIZE; i++)
    {
        if (g_arp_cache[i].valid && g_arp_cache[i].ip == ip)
        {
            mac_copy(mac_out, g_arp_cache[i].mac);
            return 0;
        }
    }
    return -1;
}

static void arp_send_request(struct netdev *dev, ip4_addr_t target_ip)
{
    struct mbuf *m = mbuf_alloc0();
    if (!m) return;

    /* Build ARP payload (Ethernet + IPv4 sizes). */
    uint8_t *p = m->data + m->data_off;
    /* Hardware type: Ethernet = 1 */
    p[0] = 0x00; p[1] = 0x01;
    /* Protocol type: IPv4 = 0x0800 */
    p[2] = 0x08; p[3] = 0x00;
    /* Hardware addr len: 6 */
    p[4] = 6;
    /* Protocol addr len: 4 */
    p[5] = 4;
    /* Opcode: request = 1 */
    p[6] = 0x00; p[7] = 0x01;
    /* Sender MAC */
    memcpy(p + 8, dev->mac, 6);
    /* Sender IP */
    memcpy(p + 14, &dev->ip, 4);
    /* Target MAC: 00:00:00:00:00:00 */
    memset(p + 18, 0, 6);
    /* Target IP */
    memcpy(p + 24, &target_ip, 4);

    m->len = 28;

    /* Save for pending lookup. */
    g_arp_pending.ip = target_ip;
    g_arp_pending.valid = 1;

    ethernet_send(dev, (const uint8_t *)"\xff\xff\xff\xff\xff\xff",
                  ETH_P_ARP, m);
}

static void arp_handle_rx(struct netdev *dev, struct mbuf *m)
{
    if (m->len < sizeof(struct arp_hdr)) { mbuf_free(m); return; }

    struct arp_hdr *ah = (struct arp_hdr *)(m->data + m->data_off);
    uint16_t opcode = be16toh(ah->opcode);

    if (ah->hw_type != htobe16(1) || ah->proto_type != htobe16(ETH_P_IP))
    {
        mbuf_free(m);
        return;
    }

    uint8_t *body = m->data + m->data_off + sizeof(struct arp_hdr);
    /* sender MAC (6) + sender IP (4) + target MAC (6) + target IP (4) = 20 */
    if (m->len < sizeof(struct arp_hdr) + 20) { mbuf_free(m); return; }

    ip4_addr_t sender_ip;
    memcpy(&sender_ip, body + 6, 4);

    if (opcode == ARP_OP_REQUEST)
    {
        /* Check if someone is asking for our IP. */
        ip4_addr_t target_ip;
        memcpy(&target_ip, body + 14, 4);
        if (target_ip == dev->ip)
        {
            /* Send reply. */
            struct mbuf *reply = mbuf_alloc0();
            if (!reply) { mbuf_free(m); return; }

            uint8_t *r = reply->data + reply->data_off;
            /* ARP header */
            r[0] = 0x00; r[1] = 0x01; /* hw type: Ethernet */
            r[2] = 0x08; r[3] = 0x00; /* proto type: IPv4 */
            r[4] = 6; r[5] = 4;
            r[6] = 0x00; r[7] = 0x02; /* opcode: reply */
            /* Sender (us) */
            memcpy(r + 8, dev->mac, 6);
            memcpy(r + 14, &dev->ip, 4);
            /* Target (requester) */
            memcpy(r + 18, body, 6);   /* requester MAC */
            memcpy(r + 24, &sender_ip, 4);

            reply->len = 28;
            ethernet_send(dev, body, ETH_P_ARP, reply);

            /* Also cache the requestor. */
            arp_cache_insert(sender_ip, body);
        }
    }
    else if (opcode == ARP_OP_REPLY)
    {
        arp_cache_insert(sender_ip, body);
    }

    mbuf_free(m);
}

int arp_resolve(struct netdev *dev, ip4_addr_t ip, uint8_t *mac_out)
{
    /* Check cache first. */
    if (arp_lookup(ip, mac_out) == 0)
        return 0;

    /* Send ARP request.  In a single-core OS we poll in a spin loop. */
    arp_send_request(dev, ip);

    /* Spin-wait for reply (up to ~500ms). */
    for (int i = 0; i < 50000; i++)
    {
        if (arp_lookup(ip, mac_out) == 0)
            return 0;
        /* Brief delay. */
        for (volatile int j = 0; j < 1000; j++) {}
    }

    return -1; /* timeout */
}

void arp_tick(void)
{
    for (int i = 0; i < ARP_CACHE_SIZE; i++)
    {
        if (g_arp_cache[i].valid)
        {
            g_arp_cache[i].age++;
            if (g_arp_cache[i].age >= ARP_TIMEOUT_TICKS)
                g_arp_cache[i].valid = 0;
        }
    }
}
