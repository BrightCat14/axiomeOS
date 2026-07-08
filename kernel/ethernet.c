#include "ethernet.h"
#include "net_util.h"
#include "string.h"
#include "printk.h"
#include "slab.h"

#define ETH_MAX_HANDLERS 8

static struct {
    uint16_t     type;
    ethertype_fn fn;
} g_eth_handlers[ETH_MAX_HANDLERS];
static int g_eth_handler_count;

void ethertype_register(uint16_t type, ethertype_fn fn)
{
    if (g_eth_handler_count >= ETH_MAX_HANDLERS) return;
    g_eth_handlers[g_eth_handler_count].type = type;
    g_eth_handlers[g_eth_handler_count].fn   = fn;
    g_eth_handler_count++;
}

int ethernet_send(struct netdev *dev, const uint8_t *dst,
                  uint16_t type, struct mbuf *payload)
{
    if (!dev || !payload) return -1;

    /* Prepend Ethernet header at the front of the first mbuf. */
    struct mbuf *hdr_m = mbuf_alloc0();
    if (!hdr_m) return -1;

    struct eth_hdr *eh = (struct eth_hdr *)(hdr_m->data + hdr_m->data_off);
    mac_copy(eh->dst, dst);
    mac_copy(eh->src, dev->mac);
    eh->type = htobe16(type);
    hdr_m->len = ETH_HDR_LEN;

    /* Chain: hdr_m → payload */
    hdr_m->next_seg = payload;

    /* Update the first mbuf to represent the whole chain. */
    size_t total = mbuf_total_len(hdr_m);

    return dev->tx(dev, hdr_m);
}

void ethernet_rx(struct netdev *dev, struct mbuf *m)
{
    if (!m || m->len < ETH_HDR_LEN) { mbuf_free(m); return; }

    struct eth_hdr *eh = (struct eth_hdr *)(m->data + m->data_off);
    uint16_t type = be16toh(eh->type);

    /* Skip the Ethernet header. */
    m->data_off += ETH_HDR_LEN;
    m->len      -= ETH_HDR_LEN;

    /* Check for broadcast (accept) or unicast/multicast (match our MAC). */
    if (!mac_is_broadcast(eh->dst) && !mac_eq(eh->dst, dev->mac))
    {
        mbuf_free(m);
        return;
    }

    /* Dispatch by ethertype. */
    for (int i = 0; i < g_eth_handler_count; i++)
    {
        if (g_eth_handlers[i].type == type)
        {
            g_eth_handlers[i].fn(dev, m);
            return;
        }
    }
    /* Unknown ethertype — drop. */
    mbuf_free(m);
}
