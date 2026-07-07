#ifndef AXIOME_LIBC_STDIO_H
#define AXIOME_LIBC_STDIO_H

#include <stddef.h>

int printf(const char *fmt, ...);
int puts(const char *s);
int putchar(int c);
int getchar(void);

#endif
