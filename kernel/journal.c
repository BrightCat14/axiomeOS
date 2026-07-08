#include "journal.h"
#include "vfs.h"
#include "slab.h"
#include "printk.h"
#include "spinlock.h"

/* In-memory ring buffer of recent log bytes. Also serves as the early-boot
   buffer (before VFS is up) and as the authoritative copy for deferred
   flushing from interrupt context. */
#define JOURNAL_RING 65536
static char journal_ring[JOURNAL_RING];

/* Monotonic producer index (next byte to write). ring_flushed is the number of
   bytes already persisted to the file; pending = ring_prod - ring_flushed. */
static size_t ring_prod;
static size_t ring_flushed;

static struct vnode *journal_file;
static size_t journal_off;
static int journal_ready;
static spinlock_t journal_lock = SPINLOCK_INIT;

volatile int g_in_irq;

static void ring_append(const char *s, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        journal_ring[ring_prod & (JOURNAL_RING - 1)] = s[i];
        ring_prod++;
    }
}

/* Write everything in the ring that has not yet reached the file. Caller must
   hold journal_lock and we must be in a context where disk I/O is safe. */
static void journal_flush_locked(void)
{
    if (!journal_ready || !journal_file)
        return;

    size_t pending = ring_prod - ring_flushed;
    if (pending > JOURNAL_RING)
    {
        ring_flushed = ring_prod - JOURNAL_RING;
        pending = JOURNAL_RING;
    }
    if (pending == 0)
        return;

    size_t start = ring_flushed & (JOURNAL_RING - 1);
    size_t first = (start + pending > JOURNAL_RING) ? (JOURNAL_RING - start)
                                                    : pending;

    size_t w1 = journal_file->sb->ops->write(journal_file->sb, journal_file,
                                             journal_off,
                                             journal_ring + start, first);
    journal_off += w1;
    if (first < pending && w1 == first)
    {
        size_t rest = pending - first;
        size_t w2 = journal_file->sb->ops->write(journal_file->sb, journal_file,
                                                 journal_off, journal_ring, rest);
        journal_off += w2;
    }
    ring_flushed += pending;
}

void journal_emit(const char *s, size_t len)
{
    if (len == 0)
        return;

    if (g_in_irq)
    {
        /* Hard IRQ: never touch the disk. Best-effort ring capture only. */
        if (!spin_trylock(&journal_lock))
            return;
        ring_append(s, len);
        spin_unlock(&journal_lock);
        return;
    }

    unsigned long flags = spin_lock_irq(&journal_lock);
    ring_append(s, len);
    journal_flush_locked();
    spin_unlock_irq(&journal_lock, flags);
}

void journal_flush(void)
{
    unsigned long flags = spin_lock_irq(&journal_lock);
    journal_flush_locked();
    spin_unlock_irq(&journal_lock, flags);
}

void journal_init_late(void)
{
    /* /etc/journal.log is pre-created via the root manifest, so at runtime we
       only need to look it up. Done WITHOUT holding journal_lock because the
       VFS path may itself call printk, and printk re-enters journal_emit which
       takes that same lock (self-deadlock). */
    struct vnode *n = vfs_lookup("/etc/journal.log", "/");

    unsigned long flags = spin_lock_irq(&journal_lock);
    if (!journal_file && n)
    {
        journal_file = n;
        journal_off = n->size;
        journal_ready = 1;
        journal_flush_locked();   /* replay early-boot ring buffer */
    }
    spin_unlock_irq(&journal_lock, flags);
}

void journal_clear(void)
{
    unsigned long flags = spin_lock_irq(&journal_lock);

    ring_prod = 0;
    ring_flushed = 0;

    if (journal_file)
    {
        journal_file->size = 0;
        journal_off = 0;
    }

    spin_unlock_irq(&journal_lock, flags);
}
