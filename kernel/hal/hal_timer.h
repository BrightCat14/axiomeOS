#ifndef AXIOME_HAL_TIMER_H
#define AXIOME_HAL_TIMER_H

#include <stdint.h>

namespace hal {

/* Periodic tick timer used for scheduling. */
class ITimer
{
public:
    /* Start a periodic timer with the given period. The meaning of
       `period_ticks` is arch-defined (e.g. APIC init count on x86_64). */
    virtual void start(uint32_t period_ticks) = 0;

    /* Number of timer interrupts fired since start(). */
    virtual uint64_t ticks(void) = 0;

    virtual ~ITimer() = default;
};

} /* namespace hal */

#endif
