#include <stdarg.h>
#include "serial.h"
#include "framebuffer.h"
#include "printk.h"
#include "vfs.h"
#include "spinlock.h"
#include "hal/cshim.h"

/* In-memory ring buffer of every byte emitted by the kernel (both printk and
   klog). Serves as the early-boot buffer (before VFS is up) and is replayed to
   /var/log/kernel.log once the root filesystem is mounted. */
#define KLOG_RING 65536
static char klog_ring[KLOG_RING];
static size_t klog_prod;
static size_t klog_flushed;
static struct vnode *klog_file;
static size_t klog_off;
static int klog_ready;
static spinlock_t klog_lock = SPINLOCK_INIT;

static void ring_append(char c)
{
    klog_ring[klog_prod & (KLOG_RING - 1)] = c;
    klog_prod++;
}

static void flush_locked(void)
{
    if (!klog_ready || !klog_file)
        return;

    size_t pending = klog_prod - klog_flushed;
    if (pending > KLOG_RING)
    {
        klog_flushed = klog_prod - KLOG_RING;
        pending = KLOG_RING;
    }
    if (pending == 0)
        return;

    size_t start = klog_flushed & (KLOG_RING - 1);
    size_t first = (start + pending > KLOG_RING) ? (KLOG_RING - start)
                                                   : pending;

    size_t w1 = klog_file->sb->ops->write(klog_file->sb, klog_file,
                                          klog_off, klog_ring + start, first);
    klog_off += w1;
    if (first < pending && w1 == first)
    {
        size_t rest = pending - first;
        size_t w2 = klog_file->sb->ops->write(klog_file->sb, klog_file,
                                              klog_off, klog_ring, rest);
        klog_off += w2;
    }
    klog_flushed += pending;
}

/* Write a character to every active console device. */
void console_putchar(char c)
{
    serial_putchar(COM1, c);
    ring_append(c);
    if (fb_active())
        fb_putchar(c);
}

void console_write(const char *s)
{
    while (*s)
        console_putchar(*s++);
}

static void print_hex(unsigned long val, int upper, void (*putch)(char));

static void emit(const char *fmt, va_list ap, void (*putch)(char))
{
    for (const char *p = fmt; *p; p++)
    {
        if (*p != '%')
        {
            putch(*p);
            continue;
        }
        p++;

        /* Flags */
        int zero_pad = 0;
        if (*p == '0') { zero_pad = 1; p++; }

        /* Width */
        int width = 0;
        while (*p >= '0' && *p <= '9')
        {
            width = width * 10 + (*p - '0');
            p++;
        }

        int long_mod = 0;
        while (*p == 'l')
        {
            long_mod = 1;
            p++;
        }

        switch (*p)
        {
            case 'd':
            case 'i':
            case 'u':
            {
                unsigned long val;
                int is_signed = (*p == 'd' || *p == 'i');
                long sval = 0;
                if (long_mod)
                    val = va_arg(ap, unsigned long);
                else if (is_signed)
                    val = (unsigned long)(sval = (long)(int)va_arg(ap, int));
                else
                    val = (unsigned long)(unsigned int)va_arg(ap, unsigned int);

                /* Handle negative signed values */
                int neg = (is_signed && (long)val < 0);
                if (neg) val = (unsigned long)(-(long)val);

                /* Render digits into a small buffer */
                char buf[24];
                int  len = 0;
                if (val == 0) { buf[len++] = '0'; }
                else {
                    unsigned long tmp = val;
                    while (tmp) { buf[len++] = (char)('0' + tmp % 10); tmp /= 10; }
                    /* reverse */
                    for (int i = 0, j = len-1; i < j; i++, j--)
                    { char t = buf[i]; buf[i] = buf[j]; buf[j] = t; }
                }

                int total = len + (neg ? 1 : 0);
                if (neg && zero_pad) putch('-');
                for (int i = total; i < width; i++)
                    putch(zero_pad ? '0' : ' ');
                if (neg && !zero_pad) putch('-');
                for (int i = 0; i < len; i++) putch(buf[i]);
                break;
            }
            case 'x':
            case 'X':
            {
                unsigned long val = long_mod
                    ? va_arg(ap, unsigned long)
                    : (unsigned long)(unsigned int)va_arg(ap, unsigned int);

                char buf[18];
                int  len = 0;
                const char *digits = (*p == 'X') ? "0123456789ABCDEF"
                                                  : "0123456789abcdef";
                if (val == 0) { buf[len++] = '0'; }
                else {
                    unsigned long tmp = val;
                    while (tmp) { buf[len++] = digits[tmp & 0xF]; tmp >>= 4; }
                    for (int i = 0, j = len-1; i < j; i++, j--)
                    { char t = buf[i]; buf[i] = buf[j]; buf[j] = t; }
                }
                for (int i = len; i < width; i++) putch(zero_pad ? '0' : ' ');
                for (int i = 0; i < len; i++) putch(buf[i]);
                break;
            }
            case 'p':
                putch('0'); putch('x');
                print_hex(va_arg(ap, unsigned long), 0, putch);
                break;
            case 's':
            {
                const char *s = va_arg(ap, const char *);
                if (!s) s = "(null)";
                int len = 0;
                for (const char *q = s; *q; q++) len++;
                for (int i = len; i < width; i++) putch(' ');
                while (*s) putch(*s++);
                break;
            }
            case 'c':
                putch((char)va_arg(ap, int));
                break;
            case '%':
                putch('%');
                break;
            default:
                putch('%');
                putch(*p);
                break;
        }
    }
}

static void put_serial(char c)
{
    serial_putchar(COM1, c);
    ring_append(c);
}

void printk(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    emit(fmt, ap, console_putchar);
    va_end(ap);
    fb_flush();
}

void klog(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    emit(fmt, ap, put_serial);
    va_end(ap);
}

static void print_hex(unsigned long val, int upper, void (*putch)(char))
{
    char buf[22];
    int i = sizeof(buf) - 1;
    buf[i] = '\0';
    if (val == 0)
        buf[--i] = '0';
    while (val)
    {
        int d = val & 0xF;
        if (d < 10)
            buf[--i] = '0' + d;
        else if (upper)
            buf[--i] = 'A' + d - 10;
        else
            buf[--i] = 'a' + d - 10;
        val >>= 4;
    }
    while (*(buf + i))
        putch(buf[i++]);
}

void klog_init_late(void)
{
    /* /var/log/kernel.log is pre-created via the root manifest, so at runtime
       we only need to look it up. Done WITHOUT holding klog_lock because the
       VFS path may itself call printk, and printk re-enters the ring buffer
       which takes that same lock (self-deadlock). */
    struct vnode *n = vfs_lookup("/var/log/kernel.log", "/");

    unsigned long flags = spin_lock_irq(&klog_lock);
    if (!klog_file && n)
    {
        klog_file = n;
        n->size = 0;            /* fresh log each boot */
        klog_off = 0;
        klog_ready = 1;
        flush_locked();         /* replay early-boot ring buffer */
    }
    spin_unlock_irq(&klog_lock, flags);
}

void klog_flush(void)
{
    unsigned long flags = spin_lock_irq(&klog_lock);
    flush_locked();
    spin_unlock_irq(&klog_lock, flags);
}

void kernel_panic(const char *msg)
{
    hal_cpu_irq_disable();
    printk("\n*** KERNEL PANIC ***\n%s\n", msg ? msg : "(no message)");
    klog("\n*** KERNEL PANIC ***\n%s\n", msg ? msg : "(no message)");
    for (;;)
        hal_cpu_halt();
}
