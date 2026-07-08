#ifndef AXIOME_JOURNAL_H
#define AXIOME_JOURNAL_H

#include <stddef.h>

/* Set by the ISR common path so the journal can avoid doing disk I/O from
   hard interrupt context (where reentering the block layer is unsafe). */
extern volatile int g_in_irq;

/* Initialize the persistent journal: open (creating if needed) /etc/journal.log
   under the axiomefs root and replay any boot-time logs captured before VFS
   was ready. Call once after the root filesystem is mounted. */
void journal_init_late(void);

/* Append a log string (not necessarily NUL-terminated) to the journal. Writes
   to the serial console are handled separately by printk; this only manages
   the in-memory ring buffer and the on-disk file. */
void journal_emit(const char *s, size_t len);

/* Flush any ring-buffered bytes that have not yet reached the file. Safe to
   call from thread context (e.g. the idle loop). */
void journal_flush(void);

/* Truncate the journal: empty the file and the in-memory ring. */
void journal_clear(void);

#endif
