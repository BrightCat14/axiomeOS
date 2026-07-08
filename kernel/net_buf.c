#include "net_buf.h"
#include "pmm.h"
#include "vmm.h"
#include "string.h"
#include "printk.h"
#include "spinlock.h"

#define MBUF_POOL_SIZE 256

static struct mbuf g_mbuf_pool[MBUF_POOL_SIZE];
static struct mbuf *g_mbuf_free;
static spinlock_t g_mbuf_lock = SPINLOCK_INIT;

/* One 4 KiB page is split into header + data for each mbuf.
   We use the first page-aligned region after the mbuf struct for data. */
#define MBUF_PAGE_SIZE 4096

void mbuf_init(void)
{
    g_mbuf_free = 0;
    memset(g_mbuf_pool, 0, sizeof(g_mbuf_pool));

    for (int i = MBUF_POOL_SIZE - 1; i >= 0; i--)
    {
        struct mbuf *m = &g_mbuf_pool[i];
        void *page = pmm_alloc_frame();
        if (!page) continue;
        m->data = (uint8_t *)vmm_mmap_phys((uint64_t)page, 1, PTE_WRITE);
        m->phys = (uint64_t)page;
        m->len = 0;
        m->data_off = 0;
        m->refcount = 1;
        m->next = g_mbuf_free;
        m->next_seg = 0;
        g_mbuf_free = m;
    }
    printk("MBUF: pool initialised (%d buffers)\n", MBUF_POOL_SIZE);
}

struct mbuf *mbuf_alloc(void)
{
    unsigned long flags = spin_lock_irq(&g_mbuf_lock);
    struct mbuf *m = g_mbuf_free;
    if (m)
    {
        g_mbuf_free = m->next;
        m->next = 0;
        m->next_seg = 0;
        m->len = 0;
        m->data_off = 0;
        m->refcount = 1;
    }
    spin_unlock_irq(&g_mbuf_lock, flags);
    return m;
}

struct mbuf *mbuf_alloc0(void)
{
    struct mbuf *m = mbuf_alloc();
    if (m && m->data)
        memset(m->data, 0, MBUF_DATA_SIZE);
    return m;
}

void mbuf_free(struct mbuf *m)
{
    if (!m) return;
    if (m->refcount > 1)
    {
        m->refcount--;
        return;
    }
    /* Free any chained segments first. */
    while (m->next_seg)
    {
        struct mbuf *seg = m->next_seg;
        m->next_seg = seg->next_seg;
        seg->refcount = 1;
        seg->next = g_mbuf_free;
        g_mbuf_free = seg;
    }
    m->refcount = 1;
    unsigned long flags = spin_lock_irq(&g_mbuf_lock);
    m->next = g_mbuf_free;
    g_mbuf_free = m;
    spin_unlock_irq(&g_mbuf_lock, flags);
}

void mbuf_ref(struct mbuf *m)
{
    if (m) m->refcount++;
}

struct mbuf *mbuf_clone(struct mbuf *m)
{
    if (!m) return 0;
    struct mbuf *c = mbuf_alloc();
    if (!c) return 0;
    c->data = m->data;
    c->phys = m->phys;
    c->data_off = m->data_off;
    c->len = m->len;
    c->refcount = 1; /* we share the buffer */
    c->next_seg = 0;
    /* Don't bump refcount — we use a separate refcount per clone.  When the
       clone is freed, the original's buffer is not returned to the pool. */
    return c;
}

int mbuf_append(struct mbuf **head, const void *src, size_t len)
{
    const uint8_t *p = (const uint8_t *)src;
    while (len > 0)
    {
        /* Find or allocate the tail mbuf. */
        struct mbuf *tail = *head;
        if (!tail)
        {
            tail = mbuf_alloc0();
            if (!tail) return -1;
            *head = tail;
        }
        while (tail->next_seg)
            tail = tail->next_seg;

        size_t avail = MBUF_DATA_SIZE - tail->len;
        if (avail == 0)
        {
            tail->next_seg = mbuf_alloc0();
            if (!tail->next_seg) return -1;
            tail = tail->next_seg;
            avail = MBUF_DATA_SIZE;
        }
        size_t n = (len < avail) ? len : avail;
        memcpy(tail->data + tail->len, p, n);
        tail->len += n;
        p += n;
        len -= n;
    }
    return 0;
}

size_t mbuf_total_len(struct mbuf *m)
{
    size_t total = 0;
    while (m) { total += m->len; m = m->next_seg; }
    return total;
}

size_t mbuf_copyout(void *dst, size_t dst_len, struct mbuf *m, size_t off)
{
    uint8_t *d = (uint8_t *)dst;
    size_t copied = 0;
    while (m && copied < dst_len)
    {
        if (off < m->len)
        {
            size_t n = m->len - off;
            if (n > dst_len - copied) n = dst_len - copied;
            memcpy(d + copied, m->data + m->data_off + off, n);
            copied += n;
            off = 0;
        }
        else
        {
            off -= m->len;
        }
        m = m->next_seg;
    }
    return copied;
}
