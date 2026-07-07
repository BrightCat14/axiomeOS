#ifndef AXIOME_SLAB_H
#define AXIOME_SLAB_H

#include <stddef.h>
#include <stdint.h>

void slab_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);

#endif
