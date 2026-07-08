#ifndef AXIOME_LIBC_STDIO_H
#define AXIOME_LIBC_STDIO_H

#include <stddef.h>
#include <stdarg.h>

int printf(const char *fmt, ...);
int puts(const char *s);
int putchar(int c);
int getchar(void);
int vprintf(const char *fmt, va_list ap);

#endif
