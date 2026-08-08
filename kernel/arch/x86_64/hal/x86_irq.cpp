#include "x86_hal.h"
#include "../../../apic.h"
#include "../../../ioapic.h"

namespace hal {
namespace x86_64 {
namespace {

struct IrqSlot
{
    IrqHandlerFn fn;
    void        *ctx;
};

/* APIC/IOAPIC-backed interrupt controller for x86_64. */
class X86IrqController final : public IIrqController
{
public:
    X86IrqController()
    {
        for (int i = 0; i < 256; i++)
        {
            slots_[i].fn = nullptr;
            slots_[i].ctx = nullptr;
        }
    }

    int register_handler(int vector, IrqHandlerFn fn, void *ctx) override
    {
        if (vector < 0 || vector > 255)
            return -1;
        slots_[vector].fn = fn;
        slots_[vector].ctx = ctx;
        return 0;
    }

    int unregister_handler(int vector) override
    {
        if (vector < 0 || vector > 255)
            return -1;
        slots_[vector].fn = nullptr;
        slots_[vector].ctx = nullptr;
        return 0;
    }

    void dispatch(int vector) override
    {
        if (vector < 0 || vector > 255)
            return;
        if (slots_[vector].fn)
            slots_[vector].fn(slots_[vector].ctx);
    }

    void eoi(void) override
    {
        apic_eoi();
    }

    void mask(int irq, int masked) override
    {
        ioapic_mask((unsigned int)irq, masked ? 1 : 0);
    }

    void route(int irq, uint8_t vector, int masked) override
    {
        ioapic_set_entry((unsigned int)irq, vector, 0, masked);
    }

private:
    IrqSlot slots_[256];
};

alignas(X86IrqController) static uint8_t g_storage[sizeof(X86IrqController)];

} /* namespace */

IIrqController *x86_irq_create(void)
{
    return new (g_storage) X86IrqController();
}

} /* namespace x86_64 */
} /* namespace hal */
