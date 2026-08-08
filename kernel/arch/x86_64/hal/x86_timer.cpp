#include "x86_hal.h"
#include "../../../apic.h"

namespace hal {
namespace x86_64 {
namespace {

/* Local APIC periodic timer for x86_64. start() also brings up the APIC
   (spurious vector, PIC masking) as part of timer initialization. */
class X86Timer final : public ITimer
{
public:
    void start(uint32_t period_ticks) override
    {
        apic_init();
        apic_timer_start(period_ticks);
    }

    uint64_t ticks(void) override
    {
        return apic_get_ticks();
    }
};

alignas(X86Timer) static uint8_t g_storage[sizeof(X86Timer)];

} /* namespace */

ITimer *x86_timer_create(void)
{
    return new (g_storage) X86Timer();
}

} /* namespace x86_64 */
} /* namespace hal */
