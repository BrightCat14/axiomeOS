#ifndef AXIOME_IO_H
#define AXIOME_IO_H

#include <stdint.h>

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port)
{
    uint16_t ret;
    __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val)
{
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port)
{
    uint32_t ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outl(uint16_t port, uint32_t val)
{
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

/* Repetitive string ins/outs for block transfers (16-bit words). */
static inline void insw(uint16_t port, void *buf, uint32_t count)
{
    __asm__ volatile("rep insw"
                     : "+D"(buf), "+c"(count)
                     : "d"(port)
                     : "memory");
}

static inline void outsw(uint16_t port, const void *buf, uint32_t count)
{
    __asm__ volatile("rep outsw"
                     : "+S"(buf), "+c"(count)
                     : "d"(port)
                     : "memory");
}

static inline void io_delay(void)
{
    /* ~1us waste via port 0x80 (ISA diagnostic). */
    __asm__ volatile("outb %%al, $0x80" : : "a"((uint8_t)0));
}

#endif
