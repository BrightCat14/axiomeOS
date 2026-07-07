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

void tty_init(void)
{
    tty_len = 0;
    tty_pos = 0;
    tty_ready = 0;
}

/* Echo to every active console device (serial + framebuffer). */
static void tty_echo(char c)
{
    console_putchar(c);
}

/* Called from interrupt context with a translated, printable-ish char. */
void tty_input_char(char c)
{
    unsigned long flags = spin_lock_irq(&tty_lock);

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
