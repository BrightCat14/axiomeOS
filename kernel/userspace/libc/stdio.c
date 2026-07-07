#include "stdio.h"
#include "syscall.h"
#include "string.h"
#include <stdarg.h>
#include <stddef.h>

#define OUTBUF_SIZE 256
static char outbuf[OUTBUF_SIZE];
static int outlen = 0;

static void flush(void)
{
    if (outlen > 0)
    {
        write(1, outbuf, (size_t)outlen);
        outlen = 0;
    }
}

static void emit(char c)
{
    outbuf[outlen++] = c;
    if (outlen >= (int)sizeof(outbuf) - 1)
        flush();
}

static void emit_str(const char *s)
{
    while (*s)
        emit(*s++);
}

static void emit_uint(unsigned long v, int base, int upper)
{
    char tmp[24];
    int i = 0;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (v == 0)
    {
        tmp[i++] = '0';
    }
    else
    {
        while (v)
        {
            tmp[i++] = digits[v % (unsigned long)base];
            v /= (unsigned long)base;
        }
    }
    while (i--)
        emit(tmp[i]);
}

static void emit_int(long v)
{
    if (v < 0)
    {
        emit('-');
        emit_uint((unsigned long)(-(v + 1)) + 1, 10, 0);
    }
    else
    {
        emit_uint((unsigned long)v, 10, 0);
    }
}

int printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    for (; *fmt; fmt++)
    {
        if (*fmt != '%')
        {
            emit(*fmt);
            continue;
        }
        fmt++;
        if (*fmt == 0)
            break;
        if (*fmt == '%')
        {
            emit('%');
            continue;
        }

        /* skip flags, width, precision, length modifiers */
        while (*fmt == '-' || *fmt == '+' || *fmt == ' ' ||
               *fmt == '#' || *fmt == '0')
            fmt++;
        while (*fmt >= '0' && *fmt <= '9')
            fmt++;
        if (*fmt == '.')
        {
            fmt++;
            while (*fmt >= '0' && *fmt <= '9')
                fmt++;
        }
        int longflag = 0;
        while (*fmt == 'l' || *fmt == 'h' || *fmt == 'z')
        {
            longflag = 1;
            fmt++;
        }
        if (*fmt == 0)
            break;

        char spec = *fmt;
        switch (spec)
        {
        case 'd':
        case 'i': {
            long v = longflag ? va_arg(ap, long) : (long)va_arg(ap, int);
            emit_int(v);
            break;
        }
        case 'u': {
            unsigned long v = longflag ? va_arg(ap, unsigned long)
                                       : (unsigned long)va_arg(ap, unsigned int);
            emit_uint(v, 10, 0);
            break;
        }
        case 'x': {
            unsigned long v = longflag ? va_arg(ap, unsigned long)
                                       : (unsigned long)va_arg(ap, unsigned int);
            emit_uint(v, 16, 0);
            break;
        }
        case 'X': {
            unsigned long v = longflag ? va_arg(ap, unsigned long)
                                       : (unsigned long)va_arg(ap, unsigned int);
            emit_uint(v, 16, 1);
            break;
        }
        case 'p': {
            void *p = va_arg(ap, void *);
            emit_str("0x");
            emit_uint((unsigned long)(unsigned long long)p, 16, 0);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            emit_str(s);
            break;
        }
        case 'c': {
            int c = va_arg(ap, int);
            emit((char)c);
            break;
        }
        default:
            emit('%');
            emit(spec);
            break;
        }
    }

    flush();
    va_end(ap);
    return 0;
}

int puts(const char *s)
{
    emit_str(s);
    emit('\n');
    flush();
    return 0;
}

int putchar(int c)
{
    char ch = (char)c;
    write(1, &ch, 1);
    return c;
}

int getchar(void)
{
    char c;
    if (read(0, &c, 1) > 0)
        return (unsigned char)c;
    return -1;
}
