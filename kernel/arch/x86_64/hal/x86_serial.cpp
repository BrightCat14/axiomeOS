#include "x86_hal.h"

namespace hal {
namespace x86_64 {
namespace {

/* 16550 UART on the classic x86 COM ports (port-mapped). */
class X86Serial final : public ISerial
{
public:
    void init(uintptr_t base) override
    {
        IPortIo &io = port_io();
        const uint16_t b = (uint16_t)base;

        io.out8(b + 3, 0x80);     /* DLAB on */
        io.out8(b + 0, 0x01);     /* divisor low  (115200 baud) */
        io.out8(b + 1, 0x00);     /* divisor high */
        io.out8(b + 3, 0x03);     /* 8N1 */
        io.out8(b + 2, 0xC7);     /* FIFO enable */
        io.out8(b + 4, 0x0B);     /* IRQ enabled, RTS/DTR */
    }

    void putc(uintptr_t base, char c) override
    {
        IPortIo &io = port_io();
        const uint16_t b = (uint16_t)base;

        if (c == '\n')
        {
            while (!(io.in8(b + 5) & 0x20))
                ;
            io.out8(b + 0, '\r');
        }

        while (!(io.in8(b + 5) & 0x20))
            ;
        io.out8(b + 0, (uint8_t)c);
    }

    void write(uintptr_t base, const char *s) override
    {
        while (*s)
            putc(base, *s++);
    }

    char getc(uintptr_t base) override
    {
        return (char)port_io().in8((uint16_t)base + 0);
    }

    int rx_ready(uintptr_t base) override
    {
        return (port_io().in8((uint16_t)base + 5) & 0x01) != 0;
    }

    void enable_input(uintptr_t base) override
    {
        IPortIo &io = port_io();
        const uint16_t b = (uint16_t)base;

        io.out8(b + 1, 0x01);     /* IER_RX */
        io.in8(b + 5);            /* clear LSR */
        io.in8(b + 0);            /* clear data */
    }
};

alignas(X86Serial) static uint8_t g_storage[sizeof(X86Serial)];

} /* namespace */

ISerial *x86_serial_create(void)
{
    return new (g_storage) X86Serial();
}

} /* namespace x86_64 */
} /* namespace hal */
