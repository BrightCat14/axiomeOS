#include "keyboard.h"
#include "printk.h"
#include "ioapic.h"
#include "tty.h"

#define KEYBOARD_IRQ 1

static const char scancode_ansi[128] = {
    [0x00] = 0, [0x01] = 27,
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5',
    [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0',
    [0x0C] = '-', [0x0D] = '=', [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't',
    [0x15] = 'y', [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
    [0x1A] = '[', [0x1B] = ']', [0x1C] = '\n',
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
    [0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l',
    [0x27] = ';', [0x28] = '\'', [0x29] = '`',
    [0x2B] = '\\',
    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v',
    [0x30] = 'b', [0x31] = 'n', [0x32] = 'm', [0x33] = ',', [0x34] = '.',
    [0x35] = '/',
    [0x37] = '*', [0x39] = ' ',
    [0x4A] = '-', [0x4E] = '+',
};

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

void keyboard_irq_handler(void)
{
    uint8_t status = inb(0x64);
    if (status & 1)
    {
        uint8_t scancode = inb(0x60);
        if (scancode < 0x80)
        {
            char c = scancode_ansi[scancode];
            if (c)
                tty_input_char(c);
        }
    }
}

static int keyboard_self_test(void)
{
    outb(0x64, 0xAA);
    for (int i = 0; i < 10000; i++)
    {
        if (inb(0x64) & 1)
            return inb(0x60) == 0x55;
    }
    return 0;
}

void keyboard_init(void)
{
    printk("Keyboard: probing PS/2 controller...\n");

    if (!keyboard_self_test())
    {
        printk("Keyboard: self-test failed (no PS/2 controller?)\n");
        return;
    }

    outb(0x64, 0x60);
    outb(0x60, 0x47);

    outb(0x60, 0xF4);
    for (int i = 0; i < 1000; i++)
    {
        if (inb(0x64) & 1)
            break;
    }
    if (inb(0x64) & 1)
        inb(0x60);

    ioapic_mask(KEYBOARD_IRQ, 0);
    printk("Keyboard: ready\n");
}
