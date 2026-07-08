#include "icmp.h"
#include "ipv4.h"
#include "net_util.h"
#include "string.h"
#include "printk.h"

void icmp_init(void)
{
    ip_proto_register(IP_PROTO_ICMP, (ip_proto_fn)icmp_rx);
}

void icmp_rx(struct netdev *dev, ip4_addr_t src, ip4_addr_t dst, struct mbuf *m)
{
    if (m->len < sizeof(struct icmp_hdr)) { mbuf_free(m); return; }

    struct icmp_hdr *icmph = (struct icmp_hdr *)(m->data + m->data_off);

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
