#include "tty.h"
#include "serial.h"
#include "framebuffer.h"
#include "printk.h"
#include "spinlock.h"
#include "sched.h"
#include "signal.h"
#include "security.h"

#define TTY_LINE_MAX 512
#define TTY_CTRL_C   3

static spinlock_t tty_lock = SPINLOCK_INIT;
static char tty_line[TTY_LINE_MAX];
static int  tty_len;
static int  tty_pos;
static int  tty_ready;

/* Thread blocked in tty_read_char_blocked() waiting for input. Woken when a
   line completes or when Ctrl-C arrives (issue #28 / #30). */
static struct thread *tty_wait_read;

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

    /* Ctrl-C: raise SIGINT on the blocked reader (and any process waiting on
       this tty) instead of buffering the byte (issue #30). */
    if (c == TTY_CTRL_C)
    {
        struct thread *r = tty_wait_read;
        if (sched_current() && sched_current()->pid)
            r = sched_current();
        if (r && r != (struct thread *)0)
        {
            r->sig_pending |= (1ULL << SIGINT);
            sched_wake(r);
        }
        tty_wait_read = 0;
        spin_unlock_irq(&tty_lock, flags);
        return;
    }

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
        if (tty_wait_read)
        {
            sched_wake(tty_wait_read);
            tty_wait_read = 0;
        }
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

/* Blocking line read (issue #28). Suspends the current thread until a full
   line is available. Returns 1 for a byte read, -EINTR if Ctrl-C pending.
   The null thread guard means kernel threads never block here. */
int tty_read_char_blocked(char *c)
{
    struct thread *self = sched_current();
    if (!self)
        return 0;

    for (;;)
    {
        if (self->sig_pending & (1ULL << SIGINT))
            return -EINTR;   /* signal delivered; let the syscall path handle it */

        unsigned long flags = spin_lock_irq(&tty_lock);
        if (tty_ready)
        {
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
        tty_wait_read = self;
        spin_unlock_irq(&tty_lock, flags);
        /* DIAGNOSTIC: busy-wait instead of suspending, to isolate the
           sched_suspend-in-syscall path as the crash source. */
        self->state = THREAD_READY;
        sched_yield();
        tty_wait_read = 0;
    }
}
