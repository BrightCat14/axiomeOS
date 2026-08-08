#include "x86_hal.h"

namespace hal {
namespace x86_64 {
namespace {

/* Privileged CPU control for x86_64. */
class X86Cpu final : public ICpu
{
public:
    void halt(void) override
    {
        __asm__ volatile("hlt");
    }

    void pause(void) override
    {
        __asm__ volatile("pause");
    }

    void irq_enable(void) override
    {
        __asm__ volatile("sti");
    }

    void irq_disable(void) override
    {
        __asm__ volatile("cli");
    }

    unsigned long save_and_disable_irqs(void) override
    {
        unsigned long flags;
        __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
        return flags;
    }

    void restore_irqs(unsigned long flags) override
    {
        __asm__ volatile("pushq %0; popfq" : : "r"(flags) : "cc", "memory");
    }

    uint64_t fault_address(void) override
    {
        uint64_t cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        return cr2;
    }

    void tlb_flush(void) override
    {
        __asm__ volatile("mov %%cr3, %%rax; mov %%rax, %%cr3" ::: "rax", "memory");
    }

    void memory_barrier(void) override
    {
        __asm__ volatile("mfence" ::: "memory");
    }
};

alignas(X86Cpu) static uint8_t g_storage[sizeof(X86Cpu)];

} /* namespace */

ICpu *x86_cpu_create(void)
{
    return new (g_storage) X86Cpu();
}

} /* namespace x86_64 */
} /* namespace hal */
