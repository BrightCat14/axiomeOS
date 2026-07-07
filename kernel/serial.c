#include "serial.h"
#include "tty.h"
#include "ioapic.h"

#define PORT_COM1 0x3F8

#define REG_DATA         0
#define REG_IER          1
#define REG_FCR          2
#define REG_LCR          3
#define REG_MCR          4
#define REG_LSR          5

#define LSR_THR_EMPTY    (1 << 5)
#define LSR_DATA_READY   (1 << 0)
#define IER_RX            (1 << 0)

static inline uint8_t inb(uint16_t port)
{
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "d"(port));
    return val;
}

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "d"(port));
}

void serial_init(int port)
{
    uint16_t base = (uint16_t)port;

    uint8_t lcr = 0x80;
    __asm__ volatile("outb %0, %1" : : "a"(lcr), "Nd"(base + REG_LCR));

    uint16_t divisor = 1;
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)(divisor & 0xFF)), "Nd"(base + REG_DATA));
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)(divisor >> 8)), "Nd"(base + REG_IER));

    lcr = 0x03;
    __asm__ volatile("outb %0, %1" : : "a"(lcr), "Nd"(base + REG_LCR));

    uint8_t fcr = 0xC7;
    __asm__ volatile("outb %0, %1" : : "a"(fcr), "Nd"(base + REG_FCR));

    uint8_t mcr = 0x0B;
    __asm__ volatile("outb %0, %1" : : "a"(mcr), "Nd"(base + REG_MCR));
}

/* Enable the COM1 received-data interrupt and route it via the IOAPIC. */
void serial_init_input(void)
{
    uint16_t base = COM1;
    outb(base + REG_IER, IER_RX);
    inb(base + REG_LSR);
    inb(base + REG_DATA);
    ioapic_mask(4, 0);
}

void serial_irq_handler(void)
{
    uint16_t base = COM1;
    for (;;)
    {
        uint8_t lsr = inb(base + REG_LSR);
        if (!(lsr & LSR_DATA_READY))
            break;
        uint8_t c = inb(base + REG_DATA);
        tty_input_char((char)c);
    }
}

void serial_putchar(int port, char c)
{
    uint16_t base = (uint16_t)port;

    if (c == '\n')
    {
        uint8_t lsr;
        do {
            __asm__ volatile("inb %1, %0" : "=a"(lsr) : "Nd"(base + REG_LSR));
        } while (!(lsr & LSR_THR_EMPTY));
        __asm__ volatile("outb %0, %1" : : "a"((uint8_t)'\r'), "Nd"(base + REG_DATA));
    }

    uint8_t lsr;
    do {
        __asm__ volatile("inb %1, %0" : "=a"(lsr) : "Nd"(base + REG_LSR));
    } while (!(lsr & LSR_THR_EMPTY));
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)c), "Nd"(base + REG_DATA));
}

void serial_write(int port, const char *s)
{
    while (*s)
        serial_putchar(port, *s++);
}
