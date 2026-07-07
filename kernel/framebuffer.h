#ifndef AXIOME_FRAMEBUFFER_H
#define AXIOME_FRAMEBUFFER_H

#include <stdint.h>

void fb_init(uintptr_t addr, uint32_t width, uint32_t height,
             uint32_t pitch, uint8_t bpp, uint8_t type);
void fb_putchar(char c);
void fb_write(const char *s);
void fb_scroll(void);
void fb_clear(void);
int fb_active(void);

#endif
