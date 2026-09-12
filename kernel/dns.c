#include "dns.h"
#include "udp.h"
#include "net_util.h"
#include "sched.h"
#include "clock.h"
#include "string.h"
#include "printk.h"

#define DNS_PORT 53
#define DNS_TYPE_A 1
#define DNS_CLASS_IN 1
#define DNS_FLAG_RD 0x0100
#define DNS_FLAG_QR 0x8000

struct dns_hdr {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} __attribute__((packed));

static uint16_t g_dns_id;

/* Encode a dot-separated hostname as DNS labels: 3www7example3com0. */
static int dns_encode_name(uint8_t *out, const char *name)
{
    int total = 0;
    const char *p = name;
    while (*p)
    {
        const char *dot = p;
        while (*dot && *dot != '.') dot++;
        size_t len = (size_t)(dot - p);
        if (len == 0 || len > 63) return -1;
        out[total++] = (uint8_t)len;
        if ((size_t)total + len > 255) return -1;
        memcpy(out + total, p, len);
        total += (int)len;
        p = dot;
        if (*p == '.') p++;
    }
    out[total++] = 0;
    return total;
}

/* Parse a dotted-quad literal.  Returns 0 and stores the address on success. */
static int dns_parse_dot4(const char *s, ip4_addr_t *out)
{
    int parts[4];
    int np = 0;
    unsigned v = 0;
    int any = 0;
    const char *p = s;
    while (*p)
    {
        if (*p >= '0' && *p <= '9')
        {
            v = v * 10 + (unsigned)(*p - '0');
            any = 1;
            if (v > 255) return -1;
        }
        else if (*p == '.')
        {
            if (!any || np >= 3) return -1;
            parts[np++] = (int)v;
            v = 0;
            any = 0;
        }
        else
        {
            return -1;
        }
        p++;
    }
    if (!any || np != 3) return -1;
    parts[np] = (int)v;
    *out = ip4_make(parts[0], parts[1], parts[2], parts[3]);
    return 0;
}

/* Skip an encoded name starting at `off` (handling compression pointers).
   Returns the offset just past the name, or -1 on malformed data. */
static int dns_skip_name(const uint8_t *buf, size_t n, int off)
{
    int pos = off;
    int jumped = 0;
    for (int guard = 0; guard < 64; guard++)
    {
        if (pos < 0 || pos >= (int)n) return -1;
        uint8_t len = buf[pos];
        if (len == 0)
            return jumped ? off : pos + 1;
        if ((len & 0xC0) == 0xC0)
        {
            if (pos + 1 >= (int)n) return -1;
            if (!jumped)
            {
                off = pos + 2;
                jumped = 1;
            }
            pos = ((int)(len & 0x3F) << 8) | buf[pos + 1];
        }
        else if ((len & 0xC0) == 0)
        {
            if (pos + 1 + len >= (int)n) return -1;
            pos += 1 + len;
        }
        else
        {
            return -1;
        }
    }
    return -1;
}

int dns_resolve(struct netdev *dev, const char *name, ip4_addr_t *ip_out)
{
    if (!name || !ip_out) return -1;
    if (dns_parse_dot4(name, ip_out) == 0)
        return 0;                     /* literal address, no network */

    if (!dev) return -1;
    ip4_addr_t server = dev->dns_server;
    if (server == 0) server = ip4_make(10, 0, 2, 3);  /* slirp fallback */

    size_t nlen = strlen(name);
    if (nlen == 0 || nlen > 255) return -1;

    uint8_t q[512];
    uint16_t id = ++g_dns_id;

    struct dns_hdr *h = (struct dns_hdr *)q;
    memset(h, 0, sizeof(*h));
    h->id = htobe16(id);
    h->flags = htobe16(DNS_FLAG_RD);
    h->qdcount = htobe16(1);

    int off = (int)sizeof(struct dns_hdr);
    int r = dns_encode_name(q + off, name);
    if (r < 0) return -1;
    off += r;
    if (off + 4 > (int)sizeof(q)) return -1;
    q[off++] = 0; q[off++] = DNS_TYPE_A;
    q[off++] = 0; q[off++] = DNS_CLASS_IN;

    int sock = udp_socket();
    if (sock < 0) return -1;
    if (udp_sendto(sock, server, DNS_PORT, q, (size_t)off) < 0)
    {
        udp_close(sock);
        return -1;
    }

    int result = -1;
    uint64_t deadline = clock_mono_ns() + 5 * 1000000000ULL;
    while (clock_mono_ns() < deadline)
    {
        if (udp_datagram_ready(sock))
        {
            uint8_t resp[1024];
            ip4_addr_t from;
            uint16_t from_port;
            int n = udp_recvfrom(sock, resp, sizeof(resp), &from, &from_port);
            if (n < (int)sizeof(struct dns_hdr)) continue;

            const struct dns_hdr *rh = (const struct dns_hdr *)resp;
            uint16_t flags = be16toh(rh->flags);
            if (be16toh(rh->id) != id || !(flags & DNS_FLAG_QR))
                continue;                     /* not our response */
            if ((flags & 0x000F) != 0)        /* non-zero rcode */
                break;

            int pos = (int)sizeof(struct dns_hdr);
            int nq = be16toh(rh->qdcount);
            for (int i = 0; i < nq; i++)
            {
                pos = dns_skip_name(resp, (size_t)n, pos);
                if (pos < 0) goto done;
                pos += 4;                     /* qtype + qclass */
            }

            int na = be16toh(rh->ancount);
            for (int i = 0; i < na; i++)
            {
                pos = dns_skip_name(resp, (size_t)n, pos);
                if (pos < 0) goto done;
                if (pos + 10 > n) goto done;
                uint16_t type = be16toh(*(const uint16_t *)(resp + pos));
                uint16_t cls = be16toh(*(const uint16_t *)(resp + pos + 2));
                uint16_t rdlen = be16toh(*(const uint16_t *)(resp + pos + 8));
                pos += 10;
                if (type == DNS_TYPE_A && cls == DNS_CLASS_IN && rdlen == 4 &&
                    pos + 4 <= n)
                {
                    uint32_t addr;
                    memcpy(&addr, resp + pos, 4);
                    *ip_out = (ip4_addr_t)addr;
                    result = 0;
                    goto done;
                }
                pos += rdlen;                 /* skip CNAME etc. */
            }
            break;
        }
        sched_yield();
    }

done:
    udp_close(sock);
    return result;
}