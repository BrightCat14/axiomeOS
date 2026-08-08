#include "x86_hal.h"
#include "../../../vmm.h"

extern uint64_t pd_table3[512];

namespace hal {
namespace x86_64 {
namespace {

/* MMIO mapping for x86_64: identity-map the 2 MiB huge page that contains
   `phys` in the bootstrap PD (pd_table3), exactly like the legacy APIC /
   IOAPIC drivers did. */
class X86Mmio final : public IMmio
{
public:
    void *map_phys(uint64_t phys, size_t size) override
    {
        (void)size;
        unsigned int pd_idx = (unsigned int)((phys >> 21) & 0x1FF);
        pd_table3[pd_idx] = phys | PTE_PRESENT | PTE_WRITE | PTE_HUGE | PTE_PCD | PTE_PWT;
        cpu().tlb_flush();
        return (void *)(uintptr_t)phys;
    }
};

alignas(X86Mmio) static uint8_t g_storage[sizeof(X86Mmio)];

} /* namespace */

IMmio *x86_mmio_create(void)
{
    return new (g_storage) X86Mmio();
}

} /* namespace x86_64 */
} /* namespace hal */
