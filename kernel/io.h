#ifndef AXIOME_IO_H
#define AXIOME_IO_H

#include <stdint.h>

#include "hal/cshim.h"

/* Port-mapped I/O. Everything routes through the HAL so drivers compiled
   against this header stay portable across architectures. */

static inline uint8_t inb(uint16_t port)
{
    return hal_port_in8(port);
}

static inline void outb(uint16_t port, uint8_t val)
{
    hal_port_out8(port, val);
}

static inline uint16_t inw(uint16_t port)
{
    return hal_port_in16(port);
}

static inline void outw(uint16_t port, uint16_t val)
{
    hal_port_out16(port, val);
}

static inline uint32_t inl(uint16_t port)
{
    return hal_port_in32(port);
}

static inline void outl(uint16_t port, uint32_t val)
{
    hal_port_out32(port, val);
}

/* Repetitive string ins/outs for block transfers (16-bit words). */
static inline void insw(uint16_t port, void *buf, uint32_t count)
{
    hal_port_ins16(port, buf, count);
}

static inline void outsw(uint16_t port, const void *buf, uint32_t count)
{
    hal_port_outs16(port, buf, count);
}

static inline void io_delay(void)
{
    hal_port_delay();
}

#endif
