#ifndef AXIOME_UDP_H
#define AXIOME_UDP_H

#include <stdint.h>
#include <stddef.h>
#include "net_buf.h"
#include "netdev.h"
#include "net_util.h"
#include "ipv4.h"

#define UDP_HDR_LEN 8

struct udp_hdr {
    uint16_t sport;
    uint16_t dport;
    uint16_t len;
    uint16_t checksum;
} __attribute__((packed));

struct udp_sock {
    uint8_t   in_use;
    uint16_t  local_port;
    struct mbuf *rx_queue;      /* received packets waiting for recv() */
    int       rx_count;
    struct thread *wait_recv;   /* thread blocked in recv() */
};

#define UDP_SOCK_MAX 32

/* Initialise UDP: registers IP protocol handler. */
void udp_init(void);

/* Create a UDP socket. Returns socket index (>=0) or -1. */
int udp_socket(void);

/* Bind a UDP socket to a local port.  `port == 0` assigns an ephemeral port
   from the dynamic range.  Returns 0 on success. */
int udp_bind(int sock, uint16_t port);

/* Send UDP datagram.  If the socket has no local port yet, one is assigned
   automatically.  Returns bytes sent or -1. */
int udp_sendto(int sock, ip4_addr_t dst_ip, uint16_t dst_port,
               const void *buf, size_t len);

/* Send a UDP datagram with an explicit device, source address and destination
   MAC (used by DHCP: 255.255.255.255 from 0.0.0.0, no ARP/routing). */
int udp_send_direct(int sock, struct netdev *dev, const uint8_t *dst_mac,
                    ip4_addr_t src_ip, ip4_addr_t dst_ip, uint16_t dst_port,
                    const void *buf, size_t len);

/* Receive UDP datagram.  Blocks until data available.  Returns bytes copied.
   Fills `src_ip`/`src_port` (may be NULL) with the sender's address. */
int udp_recvfrom(int sock, void *buf, size_t max,
                 ip4_addr_t *src_ip, uint16_t *src_port);

/* Non-blocking probe: 1 if a datagram is queued on the socket. */
int udp_datagram_ready(int sock);

/* Close a UDP socket. */
void udp_close(int sock);

/* Called when a UDP packet arrives. */
void udp_rx(struct netdev *dev, ip4_addr_t src, ip4_addr_t dst, struct mbuf *m);

#endif
