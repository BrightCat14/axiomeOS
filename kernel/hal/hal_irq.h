#ifndef AXIOME_HAL_IRQ_H
#define AXIOME_HAL_IRQ_H

#include <stdint.h>

namespace hal {

/* Device interrupt handler. ctx is the opaque context passed to
   register_handler(). */
typedef void (*IrqHandlerFn)(void *ctx);

/* Architecture interrupt plumbing. The arch ISR glue calls dispatch() for
   every device interrupt vector; EOI and line masking are provided here so
   the rest of the kernel never touches APIC/IOAPIC/PIC details directly. */
class IIrqController
{
public:
    /* Bind fn/ctx to a device vector. 0 on success, -1 on bad vector. */
    virtual int register_handler(int vector, IrqHandlerFn fn, void *ctx) = 0;
    virtual int unregister_handler(int vector) = 0;

    /* Invoke the handler registered for `vector`, if any. */
    virtual void dispatch(int vector) = 0;

    /* Acknowledge end-of-interrupt for the currently serviced vector. */
    virtual void eoi(void) = 0;

    /* Mask (masked != 0) or unmask (masked == 0) a device interrupt line. */
    virtual void mask(int irq, int masked) = 0;

    /* Route a device interrupt line to a vector and set its initial mask
       state. */
    virtual void route(int irq, uint8_t vector, int masked) = 0;

    virtual ~IIrqController() = default;
};

} /* namespace hal */

#endif
