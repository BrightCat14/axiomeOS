#ifndef AXIOME_TTY_H
#define AXIOME_TTY_H

#include <stdint.h>

/* Canonical (line-disciplined) TTY input buffer shared by the PS/2 keyboard
   and the serial console. The line discipline (echo, backspace, newline)
   is applied at input time; user processes read completed lines via
   tty_read_char(). */

void tty_init(void);
void tty_input_char(char c);   /* call from ISR context */
int  tty_read_char(char *c);   /* non-blocking; 1 if a char was read, 0 otherwise */

#endif
