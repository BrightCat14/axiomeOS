#include "tty.h"
#include "serial.h"
#include "framebuffer.h"
#include "printk.h"
#include "spinlock.h"

#define TTY_LINE_MAX 512

static spinlock_t tty_lock = SPINLOCK_INIT;
static char tty_line[TTY_LINE_MAX];
static int  tty_len;
static int  tty_pos;
static int  tty_ready;

/* Escape sequence buffer for arrow keys - we send them as raw bytes to userspace */
static char esc_buffer[16];
static int esc_pos = 0;

void tty_init(void)
{
    tty_len = 0;
    tty_pos = 0;
    tty_ready = 0;
    esc_pos = 0;
}

static void tty_echo(char c)
{
    console_putchar(c);
}

/* Send an escape sequence to userspace */
static void send_escape(const char *seq)
{
    while (*seq) {
        if (tty_len < TTY_LINE_MAX - 1) {
            tty_line[tty_len++] = *seq;
        }
        seq++;
    }
    tty_ready = 1;
}

void tty_input_char(int c)
{
    unsigned long flags = spin_lock_irq(&tty_lock);

    /* Handle special key codes */
    if (c >= 0x100) {
        /* Arrow keys and special keys - send as escape sequences */
        switch (c) {
        case TTY_KEY_UP:    send_escape("\033[A"); break;
        case TTY_KEY_DOWN:  send_escape("\033[B"); break;
        case TTY_KEY_RIGHT: send_escape("\033[C"); break;
        case TTY_KEY_LEFT:  send_escape("\033[D"); break;
        case TTY_KEY_HOME:  send_escape("\033[H"); break;
        case TTY_KEY_END:   send_escape("\033[F"); break;
        case TTY_KEY_PGUP:  send_escape("\033[5~"); break;
        case TTY_KEY_PGDN:  send_escape("\033[6~"); break;
        case TTY_KEY_INSERT: send_escape("\033[2~"); break;
        case TTY_KEY_DELETE: send_escape("\033[3~"); break;
        default: break;
        }
        spin_unlock_irq(&tty_lock, flags);
        return;
    }

    if (c == '\r' || c == '\n')
    {
        if (tty_len < TTY_LINE_MAX - 1)
            tty_line[tty_len++] = '\n';
        tty_echo('\n');
        tty_ready = 1;
    }
    else if (c == '\b' || c == 0x7F)
    {
        if (tty_len > 0 && tty_line[tty_len - 1] != '\n')
        {
            tty_len--;
            tty_echo('\b');
            tty_echo(' ');
            tty_echo('\b');
        }
    }
    else if (c >= 0x20 && c < 0x7F)
    {
        if (tty_len < TTY_LINE_MAX - 1)
        {
            tty_line[tty_len++] = c;
            tty_echo(c);
        }
    }

    spin_unlock_irq(&tty_lock, flags);
}

int tty_read_char(char *c)
{
    unsigned long flags = spin_lock_irq(&tty_lock);
    if (!tty_ready)
    {
        spin_unlock_irq(&tty_lock, flags);
        return 0;
    }
    *c = tty_line[tty_pos++];
    if (tty_pos >= tty_len)
    {
        tty_ready = 0;
        tty_len = 0;
        tty_pos = 0;
    }
    spin_unlock_irq(&tty_lock, flags);
    return 1;
}
