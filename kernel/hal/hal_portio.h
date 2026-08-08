#ifndef AXIOME_HAL_PORTIO_H
#define AXIOME_HAL_PORTIO_H

#include <stdint.h>
#include <stddef.h>

namespace hal {

/* Port-mapped I/O. Backed by x86 IN/OUT instructions on x86_64; a future
   memory-mapped architecture (ARM, RISC-V) provides a different backend or
   leaves these unused in favour of hal::IMmio. Drivers should route every
   byte/word/dword access through this interface so the same driver source
   runs on every architecture. */
class IPortIo
{
public:
    virtual uint8_t  in8(uint16_t port) = 0;
    virtual void     out8(uint16_t port, uint8_t val) = 0;
    virtual uint16_t in16(uint16_t port) = 0;
    virtual void     out16(uint16_t port, uint16_t val) = 0;
    virtual uint32_t in32(uint16_t port) = 0;
    virtual void     out32(uint16_t port, uint32_t val) = 0;

    /* Block (repetitive) transfers of 16-bit words. */
    virtual void ins16(uint16_t port, void *buf, uint32_t count) = 0;
    virtual void outs16(uint16_t port, const void *buf, uint32_t count) = 0;

    /* Short arch-defined settle delay after a port write. */
    virtual void delay(void) = 0;

    virtual ~IPortIo() = default;
};

} /* namespace hal */

#endif
