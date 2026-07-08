#include <stdarg.h>
#include "serial.h"
#include "framebuffer.h"
#include "printk.h"
#include "vfs.h"
#include "spinlock.h"

/* In-memory ring buffer of every byte emitted by the kernel (both printk and
   klog). Serves as the early-boot buffer (before VFS is up) and is replayed to
   /etc/kernel.log once the root filesystem is mounted. */
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

static void print_dec(unsigned long val, void (*putch)(char));
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
                if (long_mod)
                    print_dec(va_arg(ap, unsigned long), putch);
                else
                    print_dec(va_arg(ap, int), putch);
                break;
            case 'u':
                if (long_mod)
                    print_dec(va_arg(ap, unsigned long), putch);
                else
                    print_dec(va_arg(ap, unsigned int), putch);
                break;
            case 'x':
            case 'X':
                if (long_mod)
                    print_hex(va_arg(ap, unsigned long), *p == 'X', putch);
                else
                    print_hex(va_arg(ap, unsigned int), *p == 'X', putch);
                break;
            case 'p':
                putch('0'); putch('x');
                print_hex(va_arg(ap, unsigned long), 0, putch);
                break;
            case 's':
            {
                const char *s = va_arg(ap, const char *);
                while (*s)
                    putch(*s++);
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
}

void klog(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    emit(fmt, ap, put_serial);
    va_end(ap);
}

static void print_dec(unsigned long val, void (*putch)(char))
{
    char buf[24];
    int i = sizeof(buf) - 1;
    buf[i] = '\0';
    if (val == 0)
        buf[--i] = '0';
    while (val)
    {
        buf[--i] = '0' + (val % 10);
        val /= 10;
    }
    while (*(buf + i))
        putch(buf[i++]);
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
    /* /etc/kernel.log is pre-created via the root manifest, so at runtime we
       only need to look it up. Done WITHOUT holding klog_lock because the VFS
       path may itself call printk, and printk re-enters the ring buffer which
       takes that same lock (self-deadlock). */
    struct vnode *n = vfs_lookup("/etc/kernel.log", "/");

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
    __asm__ volatile("cli");
    printk("\n*** KERNEL PANIC ***\n%s\n", msg ? msg : "(no message)");
    klog("\n*** KERNEL PANIC ***\n%s\n", msg ? msg : "(no message)");
    for (;;)
        __asm__ volatile("hlt");
}
