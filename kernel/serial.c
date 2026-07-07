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
    asm volatile("inb %w1, %b0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outb(uint16_t port, uint8_t val)
{
    asm volatile("outb %b0, %w1" : : "a"(val), "Nd"(port));
}

void serial_init(int port)
{
    uint16_t base = (uint16_t)port;

    // DLAB = 1
    outb(base + REG_LCR, 0x80);

    // Divisor = 1 (115200 baud)
    outb(base + REG_DATA, 0x01);        // LSB
    outb(base + REG_IER, 0x00);         // MSB

    // 8 bits, no parity, 1 stop bit
    outb(base + REG_LCR, 0x03);

    // Enable FIFO
    outb(base + REG_FCR, 0xC7);

    // Enable IRQ, RTS/DTR
    outb(base + REG_MCR, 0x0B);
}

/* Enable the COM1 received-data interrupt and route it via the IOAPIC. */
void serial_init_input(void)
{
    uint16_t base = PORT_COM1;
    outb(base + REG_IER, IER_RX);
    inb(base + REG_LSR);  // Clear
    inb(base + REG_DATA); // Clear
    ioapic_mask(4, 0);
}

void serial_irq_handler(void)
{
    uint16_t base = PORT_COM1;
    while (1)
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
        while (!(inb(base + REG_LSR) & LSR_THR_EMPTY));
        outb(base + REG_DATA, '\r');
    }

    while (!(inb(base + REG_LSR) & LSR_THR_EMPTY));
    outb(base + REG_DATA, (uint8_t)c);
}

void serial_write(int port, const char *s)
{
    while (*s)
        serial_putchar(port, *s++);
}
