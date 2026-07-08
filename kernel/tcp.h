#ifndef AXIOME_TCP_H
#define AXIOME_TCP_H

#include <stdint.h>
#include <stddef.h>
#include "net_buf.h"
#include "netdev.h"
#include "net_util.h"
#include "ipv4.h"

#define TCP_HDR_LEN 20
#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

#define TCP_STATE_CLOSED     0
#define TCP_STATE_LISTEN     1
#define TCP_STATE_SYN_SENT   2
#define TCP_STATE_SYN_RECEIVED 3
#define TCP_STATE_ESTABLISHED 4
#define TCP_STATE_FIN_WAIT_1 5
#define TCP_STATE_FIN_WAIT_2 6
#define TCP_STATE_TIME_WAIT  7
#define TCP_STATE_CLOSE_WAIT 8
#define TCP_STATE_LAST_ACK   9
#define TCP_STATE_CLOSING    10

#define TCP_SOCK_MAX 32
#define TCP_WND_SIZE 65535
#define TCP_MSS     1460
#define TCP_RETX_MS 200
#define TCP_RETX_MAX 5

struct tcp_hdr {
    uint16_t sport;
    uint16_t dport;
    uint32_t seq;
    uint32_t ack;
    uint8_t  data_off;  /* data offset (4 bits) + reserved (4 bits) */
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} __attribute__((packed));

struct tcp_seg {
    uint32_t seq;
    uint32_t len;
    struct mbuf *m;        /* mbuf with the TCP payload */
    struct tcp_seg *next;
    uint32_t rexmit_time;  /* tick count when retransmit is due */
    int      rexmit_count;
};

struct tcp_sock {
    uint8_t   in_use;
    uint8_t   state;
    uint16_t  local_port;
    uint16_t  remote_port;
    ip4_addr_t remote_ip;
    struct netdev *dev;

    /* Sequence numbers. */
    uint32_t  snd_nxt;     /* next seq to send */
    uint32_t  snd_una;     /* oldest unACKed seq */
    uint32_t  rcv_nxt;     /* next expected recv seq */

    /* Receive window. */
    uint32_t  rcv_wnd;

    /* Send window. */
    uint32_t  snd_wnd;

    /* Receive buffer. */
    struct mbuf *rx_buf;
    size_t   rx_len;
    struct thread *wait_recv;

    /* Send queue (segments awaiting ACK). */
    struct tcp_seg *tx_queue;
    int tx_queue_len;

    /* Listen backlog (for server mode). */
    struct tcp_sock *parent;  /* non-NULL if this is an accepted child */
};

/* Initialise TCP: registers IP protocol handler, starts retransmit timer. */
void tcp_init(void);

/* Create a TCP socket.  Returns socket index or -1. */
int tcp_socket(void);

/* Bind to a local port. */
int tcp_bind(int sock, uint16_t port);

/* Active open: initiate connection to remote.  Blocks until ESTABLISHED. */
int tcp_connect(int sock, ip4_addr_t dst_ip, uint16_t dst_port);

/* Passive open: start listening for connections. */
int tcp_listen(int sock);

/* Accept a connection.  Blocks until a client connects.  Returns child sock. */
int tcp_accept(int sock);

/* Send data.  Returns bytes sent or -1. */
int tcp_send(int sock, const void *buf, size_t len);

/* Receive data.  Blocks until data available.  Returns bytes copied. */
int tcp_recv(int sock, void *buf, size_t max);

/* Close a TCP connection. */
int tcp_close(int sock);

/* Called for each TCP packet. */
void tcp_rx(struct netdev *dev, ip4_addr_t src, ip4_addr_t dst, struct mbuf *m);

/* Tick function for retransmission (called from timer).  `ticks_ms` is
   the current time in milliseconds. */
void tcp_tick(uint32_t ticks_ms);

#endif
