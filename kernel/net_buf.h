#ifndef AXIOME_NET_BUF_H
#define AXIOME_NET_BUF_H

#include <stdint.h>
#include <stddef.h>

/*
 * Network buffer (mbuf).
 *
 * Each mbuf owns a 2048-byte data buffer allocated from the physical page
 * allocator and mapped into kernel space.  The buffer has a fixed data region
 * and the mbuf tracks the current valid range via data_off / len.
 *
 * Layout:  [ hdr (32 bytes) ][ padding ][ data_off ... data_off+len ]
 *
 * For DMA use (e1000 RX/TX) the physical address of the data region is
 * stored in `phys` so the NIC can DMA directly into/out of the buffer.
 */

#define MBUF_DATA_SIZE 2048

struct mbuf {
    uint8_t *data;          /* virtual address of data region */
    uint64_t phys;          /* physical address of data region */
    size_t   data_off;      /* start of valid data within data[] */
    size_t   len;           /* bytes of valid data */
    uint32_t refcount;      /* reference count (clone support) */
    struct mbuf *next;      /* chain link / free-list link */
    struct mbuf *next_seg;  /* fragment chain (unused initially) */
    uint32_t rx_src_ip;     /* RX metadata: source IPv4 (net order) */
    uint16_t rx_src_port;   /* RX metadata: source transport port */
};

/* Initialise the mbuf pool (called once at boot). */
void mbuf_init(void);

/* Allocate an mbuf from the pool.  Returns NULL if exhausted. */
struct mbuf *mbuf_alloc(void);

/* Allocate an mbuf and zero the data region. */
struct mbuf *mbuf_alloc0(void);

/* Free an mbuf back to the pool (decrements refcount; frees when 0). */
void mbuf_free(struct mbuf *m);

/* Increment refcount. */
void mbuf_ref(struct mbuf *m);

/* Clone an mbuf (deep copy — the clone owns its own data buffer and fragment
   chain, so both can be freed independently). */
struct mbuf *mbuf_clone(struct mbuf *m);

/* Append `len` bytes from `src` to the tail of `m`, allocating more mbufs
   in the chain if needed.  Returns 0 on success, -1 on OOM. */
int mbuf_append(struct mbuf **head, const void *src, size_t len);

/* Total bytes across all chained mbufs. */
size_t mbuf_total_len(struct mbuf *m);

/* Copy data out of an mbuf chain into a flat buffer.  Returns bytes copied. */
size_t mbuf_copyout(void *dst, size_t dst_len, struct mbuf *m, size_t off);

/* Streaming checksum over `len` bytes of an mbuf chain starting at byte
   offset `off`.  The caller seeds the accumulator (e.g. with the TCP/UDP
   pseudo-header) and folds the returned value with csum_fold(). */
uint32_t mbuf_csum_acc(uint32_t sum, struct mbuf *m, size_t off, size_t len);

#endif
