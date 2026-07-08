#include "socket.h"
#include "udp.h"
#include "tcp.h"
#include "net_util.h"
#include "printk.h"
#include "sched.h"
#include "vfs.h"
#include "string.h"

static struct sock_entry g_sock_table[SOCKET_TABLE_SIZE];

void socket_init(void)
{
    memset(g_sock_table, 0, sizeof(g_sock_table));
}

static int sock_alloc(void)
{
    for (int i = 0; i < SOCKET_TABLE_SIZE; i++)
    {
        if (!g_sock_table[i].in_use)
        {
            g_sock_table[i].in_use = 1;
            return i;
        }
    }
    return -1;
}

int sock_create(int domain, int type, int proto)
{
    (void)proto;
    if (domain != AF_INET) return -1;

    int fd = sock_alloc();
    if (fd < 0) return -1;

    struct sock_entry *se = &g_sock_table[fd];

    if (type == SOCK_DGRAM)
    {
        int handle = udp_socket();
        if (handle < 0) { se->in_use = 0; return -1; }
        se->proto = SOCK_PROTO_UDP;
        se->handle = handle;
    }
    else if (type == SOCK_STREAM)
    {
        int handle = tcp_socket();
        if (handle < 0) { se->in_use = 0; return -1; }
        se->proto = SOCK_PROTO_TCP;
        se->handle = handle;
    }
    else
    {
        se->in_use = 0;
        return -1;
    }

    return fd;
}

int sock_bind(int fd, const struct sockaddr *addr, int addrlen)
{
    if (fd < 0 || fd >= SOCKET_TABLE_SIZE || !g_sock_table[fd].in_use)
        return -1;
    (void)addrlen;

    struct sock_entry *se = &g_sock_table[fd];
    const struct sockaddr_in *sin = (const struct sockaddr_in *)addr;
    uint16_t port = be16toh(sin->sin_port);

    if (se->proto == SOCK_PROTO_UDP)
        return udp_bind(se->handle, port);
    if (se->proto == SOCK_PROTO_TCP)
        return tcp_bind(se->handle, port);

    return -1;
}

int sock_connect(int fd, const struct sockaddr *addr, int addrlen)
{
    if (fd < 0 || fd >= SOCKET_TABLE_SIZE || !g_sock_table[fd].in_use)
        return -1;
    (void)addrlen;

    struct sock_entry *se = &g_sock_table[fd];
    const struct sockaddr_in *sin = (const struct sockaddr_in *)addr;

    if (se->proto == SOCK_PROTO_TCP)
        return tcp_connect(se->handle, sin->sin_addr, be16toh(sin->sin_port));

    /* UDP: store peer address for send/recv. */
    if (se->proto == SOCK_PROTO_UDP)
    {
        se->peer_ip = sin->sin_addr;
        se->peer_port = be16toh(sin->sin_port);
        return 0;
    }

    return -1;
}

int sock_listen(int fd)
{
    if (fd < 0 || fd >= SOCKET_TABLE_SIZE || !g_sock_table[fd].in_use)
        return -1;
    struct sock_entry *se = &g_sock_table[fd];
    if (se->proto == SOCK_PROTO_TCP)
        return tcp_listen(se->handle);
    return -1;
}

int sock_accept(int fd)
{
    if (fd < 0 || fd >= SOCKET_TABLE_SIZE || !g_sock_table[fd].in_use)
        return -1;
    struct sock_entry *se = &g_sock_table[fd];
    if (se->proto != SOCK_PROTO_TCP) return -1;

    int child = tcp_accept(se->handle);
    if (child < 0) return -1;

    /* Wrap the child TCP socket in a new fd. */
    int new_fd = sock_alloc();
    if (new_fd < 0) return -1;
    g_sock_table[new_fd].in_use = 1;
    g_sock_table[new_fd].proto = SOCK_PROTO_TCP;
    g_sock_table[new_fd].handle = child;
    return new_fd;
}

long sock_send(int fd, const void *buf, size_t len)
{
    if (fd < 0 || fd >= SOCKET_TABLE_SIZE || !g_sock_table[fd].in_use)
        return -1;

    struct sock_entry *se = &g_sock_table[fd];

    if (se->proto == SOCK_PROTO_TCP)
        return tcp_send(se->handle, buf, len);

    if (se->proto == SOCK_PROTO_UDP)
        return udp_sendto(se->handle, se->peer_ip, se->peer_port, buf, len);

    return -1;
}

long sock_recv(int fd, void *buf, size_t max)
{
    if (fd < 0 || fd >= SOCKET_TABLE_SIZE || !g_sock_table[fd].in_use)
        return -1;

    struct sock_entry *se = &g_sock_table[fd];

    if (se->proto == SOCK_PROTO_TCP)
        return tcp_recv(se->handle, buf, max);

    if (se->proto == SOCK_PROTO_UDP)
    {
        ip4_addr_t src_ip;
        uint16_t src_port;
        return udp_recvfrom(se->handle, buf, max, &src_ip, &src_port);
    }

    return -1;
}

int sock_close(int fd)
{
    if (fd < 0 || fd >= SOCKET_TABLE_SIZE || !g_sock_table[fd].in_use)
        return -1;

    struct sock_entry *se = &g_sock_table[fd];

    if (se->proto == SOCK_PROTO_UDP)
        udp_close(se->handle);
    else if (se->proto == SOCK_PROTO_TCP)
        tcp_close(se->handle);

    se->in_use = 0;
    return 0;
}
