#ifndef AXIOME_SOCKET_H
#define AXIOME_SOCKET_H

#include <stdint.h>
#include <stddef.h>
#include "net_util.h"

#define AF_INET  2
#define SOCK_DGRAM  2
#define SOCK_STREAM 1

struct sockaddr {
    uint16_t sa_family;
    char     sa_data[14];
};

struct sockaddr_in {
    uint16_t sin_family;
    uint16_t sin_port;
    ip4_addr_t sin_addr;
    char     sin_zero[8];
};

/* Socket table entry — maps an fd to a socket. */
#define SOCKET_TABLE_SIZE 64

enum sock_proto { SOCK_PROTO_NONE = 0, SOCK_PROTO_UDP, SOCK_PROTO_TCP };

struct sock_entry {
    int        in_use;
    int        proto;     /* SOCK_PROTO_UDP / SOCK_PROTO_TCP */
    int        handle;    /* index into udp_sock / tcp_sock table */
    ip4_addr_t peer_ip;   /* connected peer (UDP) */
    uint16_t   peer_port;
};

/* Initialise the socket layer. */
void socket_init(void);

/* Create a socket. Returns fd (>=0) or -1. */
int sock_create(int domain, int type, int proto);

/* Bind a socket. */
int sock_bind(int fd, const struct sockaddr *addr, int addrlen);

/* Connect (TCP only). */
int sock_connect(int fd, const struct sockaddr *addr, int addrlen);

/* Listen + accept (TCP only). */
int sock_listen(int fd);
int sock_accept(int fd);

/* Send data. */
long sock_send(int fd, const void *buf, size_t len);

/* Receive data. */
long sock_recv(int fd, void *buf, size_t max);

/* Close a socket. */
int sock_close(int fd);

#endif
