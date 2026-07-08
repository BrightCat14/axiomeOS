#include "stdlib.h"
#include "errno.h"
#include "syscall.h"
#include <stddef.h>

#define HEAP_SIZE (4UL * 1024 * 1024)
static unsigned char heap[HEAP_SIZE];
static unsigned long heap_off = 0;

void *malloc(size_t n)
{
    if (n == 0)
        return 0;
    unsigned long sz = (unsigned long)n;
    sz = (sz + 15UL) & ~15UL;
    if (heap_off + sz > HEAP_SIZE)
    {
        errno = ENOMEM;
        return 0;
    }
    void *p = &heap[heap_off];
    heap_off += sz;
    return p;
}

void free(void *p)
{
    (void)p;
}

int atoi(const char *s)
{
    int neg = 0;
    int v = 0;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')
        s++;
    if (*s == '-')
    {
        neg = 1;
        s++;
    }
    else if (*s == '+')
    {
        s++;
    }
    while (*s >= '0' && *s <= '9')
    {
        v = v * 10 + (*s - '0');
        s++;
    }
    return neg ? -v : v;
}

long atol(const char *s)
{
    int neg = 0;
    long v = 0;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')
        s++;
    if (*s == '-')
    {
        neg = 1;
        s++;
    }
    else if (*s == '+')
    {
        s++;
    }
    while (*s >= '0' && *s <= '9')
    {
        v = v * 10 + (*s - '0');
        s++;
    }
    return neg ? -v : v;
}

void abort(void)
{
    syscall(SYS_EXIT, 134, 0, 0, 0, 0, 0);
    for (;;)
        __asm__ volatile ("hlt");
}

void exit(int status)
{
    syscall(SYS_EXIT, status, 0, 0, 0, 0, 0);
    for (;;)
        __asm__ volatile ("hlt");
}
