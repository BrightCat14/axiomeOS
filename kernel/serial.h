#ifndef AXIOME_SERIAL_H
#define AXIOME_SERIAL_H

#include <stdint.h>

#define COM1 0x3F8
#define COM2 0x2F8

void serial_init(int port);
void serial_init_input(void);
void serial_irq_handler(void);
void serial_putchar(int port, char c);
void serial_write(int port, const char *s);

#endif
