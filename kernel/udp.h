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

/* Bind a UDP socket to a local port.  Returns 0 on success. */
int udp_bind(int sock, uint16_t port);

/* Send UDP datagram.  Returns bytes sent or -1. */
int udp_sendto(int sock, ip4_addr_t dst_ip, uint16_t dst_port,
               const void *buf, size_t len);

/* Receive UDP datagram.  Blocks until data available.  Returns bytes copied. */
int udp_recvfrom(int sock, void *buf, size_t max,
                 ip4_addr_t *src_ip, uint16_t *src_port);

/* Close a UDP socket. */
void udp_close(int sock);

/* Called when a UDP packet arrives. */
void udp_rx(struct netdev *dev, ip4_addr_t src, ip4_addr_t dst, struct mbuf *m);

#endif
