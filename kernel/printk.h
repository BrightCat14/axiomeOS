#ifndef AXIOME_PRINTK_H
#define AXIOME_PRINTK_H

void printk(const char *fmt, ...);

/* Unrecoverable fatal error: print the message and halt the CPU. */
void kernel_panic(const char *msg);

/* Like printk, but suppressed on the framebuffer (serial + kernel log only).
   Use for noisy chatter (e.g. scheduler bookkeeping) that must not disturb the
   visible console. */
void klog(const char *fmt, ...);

void console_putchar(char c);
void console_write(const char *s);

/* Open /var/log/kernel.log under the mounted root and replay the in-memory
   log buffer captured during early boot. Call once after the root FS is mounted. */
void klog_init_late(void);

/* Flush any ring-buffered log bytes that have not yet reached the file. */
void klog_flush(void);

#endif
