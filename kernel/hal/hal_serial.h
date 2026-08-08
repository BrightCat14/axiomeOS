#ifndef AXIOME_HAL_SERIAL_H
#define AXIOME_HAL_SERIAL_H

#include <stdint.h>

namespace hal {

/* Console serial port. `base` is the platform address of the port: a
   port-mapped I/O address on x86_64, an MMIO address on other
   architectures. */
class ISerial
{
public:
    virtual void init(uintptr_t base) = 0;
    virtual void putc(uintptr_t base, char c) = 0;
    virtual void write(uintptr_t base, const char *s) = 0;
    virtual char getc(uintptr_t base) = 0;
    virtual int  rx_ready(uintptr_t base) = 0;

    /* Enable the receive-interrupt path for `base`. */
    virtual void enable_input(uintptr_t base) = 0;

    virtual ~ISerial() = default;
};

} /* namespace hal */

#endif
