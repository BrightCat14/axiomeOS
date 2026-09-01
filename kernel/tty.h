#ifndef AXIOME_TTY_H
#define AXIOME_TTY_H

#include <stdint.h>

struct thread;   /* forward decl (for the blocking-read wakeup) */

enum tty_key_code {
    TTY_KEY_UP = 0x100,
    TTY_KEY_DOWN,
    TTY_KEY_LEFT,
    TTY_KEY_RIGHT,
    TTY_KEY_HOME,
    TTY_KEY_END,
    TTY_KEY_PGUP,
    TTY_KEY_PGDN,
    TTY_KEY_INSERT,
    TTY_KEY_DELETE,
};

/* Canonical (line-disciplined) TTY input buffer shared by the PS/2 keyboard
   and the serial console. The line discipline (echo, backspace, newline)
   is applied at input time; user processes read completed lines via
   tty_read_char(). */

void tty_init(void);
void tty_input_char(int c);    /* call from ISR or polled-driver context */
int  tty_read_char(char *c);   /* non-blocking; 1 if a char was read, 0 otherwise */

/* Blocking variant: suspends the calling thread until a full line is ready
   (issue #28). Returns 1 if a char was read, or <0 if the reader was
   interrupted by a signal (e.g. Ctrl-C -> SIGINT, issue #30). */
int  tty_read_char_blocked(char *c);

#endif
