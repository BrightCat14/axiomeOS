#ifndef AXIOME_LIBC_STDLIB_H
#define AXIOME_LIBC_STDLIB_H

#include <stddef.h>

void *malloc(size_t n);
void free(void *p);
int   atoi(const char *s);
void  abort(void);

#endif
