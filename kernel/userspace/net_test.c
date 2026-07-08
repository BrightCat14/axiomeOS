/*
 * net_test.c — userspace network test
 *
 * Creates a TCP socket, connects to 10.0.2.2:80 (QEMU host), sends an
 * HTTP GET request, and prints the first few bytes of the response.
 *
 * If the network is not reachable, it will timeout after a few seconds.
 */

#include "../libc/syscall.h"
#include "../libc/string.h"
#include <stddef.h>

/* htons / htonl for little-endian x86 */
static unsigned short htons(unsigned short v) { return (v >> 8) | (v << 8); }
static unsigned int htonl(unsigned int v)
{
    return ((v >> 24) & 0xFF) | ((v >> 8) & 0xFF00) |
           ((v << 8) & 0xFF0000) | ((v << 24) & 0xFF000000);
}

static int test_loopback(void)
{
    write(1, "  [1] Loopback TCP test (127.0.0.1:8080)...\n", 44);

    int fd = syscall(SYS_SOCKET_CREATE, AF_INET, SOCK_STREAM, 0, 0, 0, 0);
    if (fd < 0)
    {
        write(1, "      FAIL: socket_create\n", 26);
        return -1;
    }
    write(1, "      socket created (fd=", 24);
    /* crude int-to-string */
    char num[4] = "?";
    num[0] = '0' + fd;
    write(1, num, 1);
    write(1, ")\n", 2);

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    addr.sin_addr = htonl(0x7F000001);  /* 127.0.0.1 */
    for (int i = 0; i < 8; i++) addr.sin_zero[i] = 0;

    int r = syscall(SYS_SOCKET_BIND, fd, (long)&addr, 16, 0, 0, 0);
    write(1, r == 0 ? "      bind OK\n" : "      bind FAIL\n", 16);

    r = syscall(SYS_SOCKET_LISTEN, fd, 0, 0, 0, 0, 0);
    write(1, r == 0 ? "      listen OK\n" : "      listen FAIL\n", 17);

    write(1, "      waiting for connection (0.5s timeout)...\n", 44);

    /* Poll for an accept with timeout. */
    int cfd = -1;
    for (int t = 0; t < 5000 && cfd < 0; t++)
    {
        cfd = syscall(SYS_SOCKET_ACCEPT, fd, 0, 0, 0, 0, 0);
        if (cfd < 0)
        {
            for (volatile int w = 0; w < 500; w++) {}
        }
    }

    if (cfd >= 0)
    {
        write(1, "      accepted!\n", 16);
        char buf[256];
        long n = syscall(SYS_SOCKET_RECV, cfd, (long)buf, 256, 0, 0, 0);
        if (n > 0)
        {
            write(1, "      received: ", 16);
            write(1, buf, n < 256 ? n : 256);
            write(1, "\n", 1);
        }
        syscall(SYS_SOCKET_CLOSE, cfd, 0, 0, 0, 0, 0);
    }
    else
    {
        write(1, "      no connection (expected if no client)\n", 43);
    }

    syscall(SYS_SOCKET_CLOSE, fd, 0, 0, 0, 0, 0);
    return 0;
}

static int test_connect(void)
{
    write(1, "  [2] TCP connect to 10.0.2.1:8080 (host)...\n", 45);

    int fd = syscall(SYS_SOCKET_CREATE, AF_INET, SOCK_STREAM, 0, 0, 0, 0);
    if (fd < 0)
    {
        write(1, "      FAIL: socket_create\n", 26);
        return -1;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    addr.sin_addr = htonl(0x0A000201);  /* 10.0.2.1 (host on tap0) */
    for (int i = 0; i < 8; i++) addr.sin_zero[i] = 0;

    write(1, "      connecting...\n", 19);
    int r = syscall(SYS_SOCKET_CONNECT, fd, (long)&addr, 16, 0, 0, 0);
    if (r < 0)
    {
        write(1, "      connect failed (timeout or refused)\n", 41);
        syscall(SYS_SOCKET_CLOSE, fd, 0, 0, 0, 0, 0);
        return -1;
    }
    write(1, "      connected!\n", 17);

    /* Send HTTP GET. */
    const char *req = "GET / HTTP/1.0\r\nHost: 10.0.2.2\r\n\r\n";
    syscall(SYS_SOCKET_SEND, fd, (long)req, 36, 0, 0, 0);
    write(1, "      sent HTTP GET\n", 19);

    /* Receive response. */
    char buf[1024];
    long total = 0;
    for (int attempt = 0; attempt < 100 && total < (long)sizeof(buf); attempt++)
    {
        long n = syscall(SYS_SOCKET_RECV, fd, (long)(buf + total),
                         sizeof(buf) - (size_t)total, 0, 0, 0);
        if (n > 0)
        {
            total += n;
        }
        else
        {
            break;
        }
    }

    if (total > 0)
    {
        write(1, "      received ", 14);
        /* Print first 200 chars of response. */
        long print_len = total < 200 ? total : 200;
        write(1, buf, (size_t)print_len);
        write(1, "\n", 1);
    }
    else
    {
        write(1, "      no response\n", 18);
    }

    syscall(SYS_SOCKET_CLOSE, fd, 0, 0, 0, 0, 0);
    return 0;
}

int main(void)
{
    write(1, "\n=== net_test ===\n", 18);
    test_loopback();
    test_connect();
    write(1, "=== net_test done ===\n", 21);
    syscall(SYS_EXIT, 0, 0, 0, 0, 0, 0);
    return 0;
}
