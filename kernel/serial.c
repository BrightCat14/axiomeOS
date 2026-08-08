#include "serial.h"
#include "tty.h"
#include "hal/cshim.h"

/* The 16550 UART is owned by the HAL (hal::x86_64::X86Serial). This file
   keeps the C serial_* API for the rest of the kernel and forwards every
   call to the portable HAL interface. */

void serial_init(int port)
{
    hal_serial_init((uintptr_t)port);
}

/* Enable the COM1 received-data interrupt and unmask its IRQ line. */
void serial_init_input(void)
{
    hal_serial_enable_input((uintptr_t)COM1);
    hal_irq_mask(4, 0);
}

void serial_irq_handler(void)
{
    uintptr_t base = COM1;
    while (hal_serial_rx_ready(base))
    {
        char c = hal_serial_getc(base);
        tty_input_char(c);
    }
}

void serial_putchar(int port, char c)
{
    hal_serial_putc((uintptr_t)port, c);
}

void serial_write(int port, const char *s)
{
    hal_serial_write((uintptr_t)port, s);
}

int serial_rx_ready(int port)
{
    return hal_serial_rx_ready((uintptr_t)port);
}

char serial_getc(int port)
{
    return hal_serial_getc((uintptr_t)port);
}
