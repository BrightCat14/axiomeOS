#ifndef AXIOME_HAL_CPU_H
#define AXIOME_HAL_CPU_H

#include <stdint.h>

namespace hal {

/* Per-CPU control. Everything here maps to privileged instructions that
   differ from architecture to architecture. */
class ICpu
{
public:
    /* Stop the CPU until the next interrupt. */
    virtual void halt(void) = 0;

    /* Hint that the CPU is spinning (e.g. x86 `pause`, ARM64 `yield`). */
    virtual void pause(void) = 0;

    /* Enable / disable maskable interrupts. */
    virtual void irq_enable(void) = 0;
    virtual void irq_disable(void) = 0;

    /* Atomically save the interrupt state, disable maskable interrupts and
       return the saved flags; restore_irqs() puts them back. Keeps code that
       needs critical sections (e.g. spinlocks) out of the assembly. */
    virtual unsigned long save_and_disable_irqs(void) = 0;
    virtual void restore_irqs(unsigned long flags) = 0;

    /* Address of the last faulting memory access (e.g. x86 CR2 for page
       faults). 0 if the architecture tracks faults differently. */
    virtual uint64_t fault_address(void) = 0;

    /* Invalidate the entire translation lookaside buffer. */
    virtual void tlb_flush(void) = 0;

    /* Full memory barrier. */
    virtual void memory_barrier(void) = 0;

    virtual ~ICpu() = default;
};

} /* namespace hal */

#endif
