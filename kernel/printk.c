#include <stdarg.h>
#include "serial.h"
#include "framebuffer.h"
#include "printk.h"

/* Write a character to every active console device. */
void console_putchar(char c)
{
    serial_putchar(COM1, c);
    if (fb_active())
        fb_putchar(c);
}

void console_write(const char *s)
{
    while (*s)
        console_putchar(*s++);
}

static void print_dec(unsigned long val)
{
    char buf[24];
    int i = sizeof(buf) - 1;
    buf[i] = '\0';
    if (val == 0)
        buf[--i] = '0';
    while (val)
    {
        buf[--i] = '0' + (val % 10);
        val /= 10;
    }
    console_write(buf + i);
}

static void print_hex(unsigned long val, int upper)
{
    char buf[22];
    int i = sizeof(buf) - 1;
    buf[i] = '\0';
    if (val == 0)
        buf[--i] = '0';
    while (val)
    {
        int d = val & 0xF;
        if (d < 10)
            buf[--i] = '0' + d;
        else if (upper)
            buf[--i] = 'A' + d - 10;
        else
            buf[--i] = 'a' + d - 10;
        val >>= 4;
    }
    console_write(buf + i);
}

void printk(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    for (const char *p = fmt; *p; p++)
    {
        if (*p != '%')
        {
            console_putchar(*p);
            continue;
        }
        p++;
        int long_mod = 0;
        while (*p == 'l')
        {
            long_mod = 1;
            p++;
        }
        switch (*p)
        {
            case 'd':
            case 'i':
                if (long_mod)
                    print_dec(va_arg(ap, unsigned long));
                else
                    print_dec(va_arg(ap, int));
                break;
            case 'u':
                if (long_mod)
                    print_dec(va_arg(ap, unsigned long));
                else
                    print_dec(va_arg(ap, unsigned int));
                break;
            case 'x':
            case 'X':
                if (long_mod)
                    print_hex(va_arg(ap, unsigned long), *p == 'X');
                else
                    print_hex(va_arg(ap, unsigned int), *p == 'X');
                break;
            case 'p':
                console_write("0x");
                print_hex(va_arg(ap, unsigned long), 0);
                break;
            case 's':
                console_write(va_arg(ap, const char *));
                break;
            case 'c':
                console_putchar((char)va_arg(ap, int));
                break;
            case '%':
                console_putchar('%');
                break;
            default:
                console_putchar('%');
                console_putchar(*p);
                break;
        }
    }

    va_end(ap);
}
