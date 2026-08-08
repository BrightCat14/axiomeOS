#include "x86_hal.h"

namespace hal {
namespace x86_64 {
namespace {

/* IN/OUT port I/O for x86_64. */
class X86PortIo final : public IPortIo
{
public:
    uint8_t in8(uint16_t port) override
    {
        uint8_t ret;
        __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
        return ret;
    }

    void out8(uint16_t port, uint8_t val) override
    {
        __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
    }

    uint16_t in16(uint16_t port) override
    {
        uint16_t ret;
        __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
        return ret;
    }

    void out16(uint16_t port, uint16_t val) override
    {
        __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
    }

    uint32_t in32(uint16_t port) override
    {
        uint32_t ret;
        __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
        return ret;
    }

    void out32(uint16_t port, uint32_t val) override
    {
        __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
    }

    void ins16(uint16_t port, void *buf, uint32_t count) override
    {
        __asm__ volatile("rep insw" : "+D"(buf), "+c"(count) : "d"(port) : "memory");
    }

    void outs16(uint16_t port, const void *buf, uint32_t count) override
    {
        __asm__ volatile("rep outsw" : "+S"(buf), "+c"(count) : "d"(port) : "memory");
    }

    void delay(void) override
    {
        __asm__ volatile("outb %%al, $0x80" : : "a"((uint8_t)0));
    }
};

alignas(X86PortIo) static uint8_t g_storage[sizeof(X86PortIo)];

} /* namespace */

IPortIo *x86_portio_create(void)
{
    return new (g_storage) X86PortIo();
}

} /* namespace x86_64 */
} /* namespace hal */
