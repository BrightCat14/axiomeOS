/* ============================================================================
 * NVMe driver (.kxt loadable kernel module)
 *
 * Implements a minimal but functional NVM Express (NVMe) 1.3 host driver,
 * following the OSDev "NVMe" guide.  It:
 *
 *   1. Probes the PCI function (class 0x01, subclass 0x08), enables bus
 *      mastering + memory space, and maps the 64-bit BAR0 register space as
 *      uncacheable (UC) memory — required for MMIO device registers.
 *   2. Resets the controller, programs the admin queues (ASQ/ACQ), enables it.
 *   3. Sends the admin Identify command for the controller and for namespace 1.
 *   4. Creates one I/O submission queue and one I/O completion queue.
 *   5. Exposes namespace 1 as a block device (/dev/nvme0n1) via the driver
 *      framework's dev_ops read/write.
 *
 * DESIGN NOTE — polled completions:
 *   The kernel has no generic per-vector IRQ registration (only a few fixed
 *   ISRs are wired at 0x20..0x24).  Like the e1000 NIC module, this driver
 *   therefore runs the completion queues in *polled* mode: all interrupts are
 *   masked via INTMS, and the driver reaps completion entries by watching the
 *   phase tag in the completion queue (see the "IRQ handler" checklist item on
 *   the OSDev page).  This is fully correct and avoids any dependency on the
 *   IRQ routing that does not exist yet.
 *
 * Phase tag layout (NVMe spec vs QEMU):
 *   The NVMe spec places the phase tag in bit 15 and the status code in
 *   bits 14:0 of the completion queue entry's status field.  Early QEMU
 *   versions (<= 8.x) reversed this: phase in bit 0, status << 1.  This
 *   driver now conforms to the spec (phase = status >> 15).
 *
 * Memory ordering:
 *   Queue/PRP buffers are normal cacheable RAM (DMA is cache-coherent on x86).
 *   The doorbell writes go to UC registers.  Because a UC store may become
 *   globally visible before an older WB store, an `mfence` is issued before
 *   every doorbell ring so the controller always sees the command it is told
 *   to consume.
 * ========================================================================== */

#include "printk.h"
#include "module.h"
#include "pci.h"
#include "vmm.h"
#include "pmm.h"
#include "slab.h"
#include "driver.h"
#include "spinlock.h"
#include "string.h"

/* ---- NVMe register offsets (BAR0) ---- */
#define NVME_REG_CAP    0x00   /* Controller Capabilities (64-bit)      */
#define NVME_REG_VS     0x08   /* Version                              */
#define NVME_REG_INTMS  0x0C   /* Interrupt Mask Set                   */
#define NVME_REG_INTMC  0x10   /* Interrupt Mask Clear                 */
#define NVME_REG_CC     0x14   /* Controller Configuration             */
#define NVME_REG_CSTS   0x1C   /* Controller Status                    */
#define NVME_REG_AQA    0x24   /* Admin Queue Attributes              */
#define NVME_REG_ASQ    0x28   /* Admin Submission Queue base (64-bit) */
#define NVME_REG_ACQ    0x30   /* Admin Completion Queue base (64-bit) */

/* CC (Controller Configuration) bit fields. */
#define NVME_CC_EN       (1u << 0)    /* Enable                              */
#define NVME_CC_CSS_NVM  (0u << 4)    /* Command Set = NVM (bits 4-6)       */
#define NVME_CC_MPS_4K   (0u << 7)    /* Memory Page Size = 4 KiB (bits 7-10)*/
#define NVME_CC_AMS_RR   (0u << 11)   /* Arbitration = Round Robin (11-13)  */
#define NVME_CC_IOSQES   (6u << 16)   /* I/O SQ entry size = 2^6 = 64 B      */
#define NVME_CC_IOCQES   (4u << 20)   /* I/O CQ entry size = 2^4 = 16 B      */

/* CSTS (Controller Status) bit fields. */
#define NVME_CSTS_RDY    (1u << 0)    /* Ready                              */
#define NVME_CSTS_SHST_SHIFT 2        /* Shutdown Status (2 bits)           */
#define NVME_CSTS_SHST(x) (((x) >> 2) & 0x3)
#define NVME_CSTS_SHST_CMPLT 0x2       /* Shutdown complete                  */

/* Admin opcodes. */
#define NVME_ADM_DELETE_SQ  0x00
#define NVME_ADM_CREATE_SQ  0x01
#define NVME_ADM_DELETE_CQ  0x04
#define NVME_ADM_CREATE_CQ  0x05
#define NVME_ADM_IDENTIFY   0x06

/* I/O opcodes. */
#define NVME_IO_WRITE  0x01
#define NVME_IO_READ   0x02

/* Queue sizes (zero-based count-1 is programmed; we use 64 entries). */
#define NVME_QUEUE_SIZE 64
#define NVME_QUEUE_QSIZE (NVME_QUEUE_SIZE - 1)

/* Identify CNS values. */
#define NVME_IDENTIFY_NS     0x00   /* CNS=0: Identify Namespace           */
#define NVME_IDENTIFY_CTRL   0x01   /* CNS=1: Identify Controller          */

/* Door-bell register base and layout. */
#define NVME_DOORBELL_BASE 0x1000

/* How many pages of bounce buffer to allocate for a single data transfer. */
#define NVME_BOUNCE_PAGES 16            /* 64 KiB */
#define NVME_BOUNCE_BYTES (NVME_BOUNCE_PAGES * PAGE_SIZE)

/* Page-table bits used to mark the MMIO BAR uncacheable (UC). */
#define NVME_PTE_UC (PTE_WRITE | PTE_PWT | PTE_PCD)

/* ---- Data structures (see OSDev "Data structures") ---- */

/* Submission Queue Entry — 64 bytes. */
struct nvme_sqe {
    uint32_t cdw0;     /* opcode | (fused<<8) | (psdt<<14) | (cid<<16) */
    uint32_t nsid;
    uint32_t rsvd2;
    uint32_t rsvd3;
    uint64_t mptr;     /* metadata pointer (unused -> 0)               */
    uint64_t prp1;     /* PRP entry 1                                   */
    uint64_t prp2;     /* PRP entry 2 (or PRP list pointer)             */
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} __attribute__((packed));

/* Completion Queue Entry — 16 bytes. */
struct nvme_cqe {
    uint32_t cdw0;     /* command-specific                             */
    uint32_t rsvd1;
    uint16_t sq_head;  /* submission queue head pointer                */
    uint16_t sq_id;
    uint16_t cid;      /* command identifier echoed back              */
    uint16_t status;   /* bit 15 = Phase tag, bits 14:0 = status code  */
} __attribute__((packed));

/* One NVMe queue (submission + completion pair). */
struct nvme_queue {
    volatile struct nvme_sqe *sq;   /* virtual base of submission ring   */
    uint64_t sq_phys;               /* physical base of submission ring   */
    volatile struct nvme_cqe *cq;   /* virtual base of completion ring    */
    uint64_t cq_phys;               /* physical base of completion ring   */
    uint16_t qid;
    uint16_t size;                  /* number of entries                  */
    uint16_t sq_tail;               /* producer index                     */
    uint16_t cq_head;               /* consumer index                     */
    uint8_t  cq_phase;              /* expected phase tag of next entry   */
    uint32_t sq_dbl;                /* doorbell register offset (SQ tail) */
    uint32_t cq_dbl;                /* doorbell register offset (CQ head) */
};

/* Physically-contiguous DMA page we allocated (to free on unload). */
struct nvme_dma {
    void    *virt;
    uint64_t phys;
    uint64_t npages;
};

struct nvme_softc {
    volatile uint8_t *regs;         /* virtual MMIO base (BAR0, UC)       */
    uint64_t regs_phys;
    uint64_t regs_pages;

    struct nvme_queue admin;        /* queue id 0                         */
    struct nvme_queue io;           /* queue id 1                         */
    uint16_t admin_cid;             /* free-running command id           */

    uint64_t dstrd;                 /* doorbell stride (bytes)           */

    /* Bounce buffer + PRP list page for data transfers. */
    struct nvme_dma bounce;
    struct nvme_dma prplist;

    /* Queue ring buffers. */
    struct nvme_dma admin_sq;
    struct nvme_dma admin_cq;
    struct nvme_dma io_sq;
    struct nvme_dma io_cq;

    /* Namespace geometry. */
    uint32_t nsid;
    uint64_t nsze;                  /* total logical blocks              */
    uint32_t block_size;            /* bytes per logical block           */
    uint32_t block_shift;           /* log2(block_size)                  */
    uint32_t max_xfer;              /* max bytes per command             */

    int ready;
};

static struct nvme_softc g_nvme;
static struct device     *g_nvme_dev;
static spinlock_t g_nvme_lock = SPINLOCK_INIT;

/* ---------------------------------------------------------------------------
 * Small helpers
 * ------------------------------------------------------------------------- */

/* Memory fence: ensures all prior loads/stores are globally visible before the
 * following doorbell store.  mfence orders both loads and stores. */
static inline void nvme_mb(void)
{
    __asm__ volatile("mfence" ::: "memory");
}

static inline uint32_t nvme_read32(struct nvme_softc *s, uint32_t off)
{
    return *(volatile uint32_t *)(s->regs + off);
}

static inline void nvme_write32(struct nvme_softc *s, uint32_t off, uint32_t v)
{
    *(volatile uint32_t *)(s->regs + off) = v;
}

/* Read a 64-bit controller register as two 32-bit accesses (the MMIO access
 * rules forbid anything wider than a 32-bit aligned read). */
static inline uint64_t nvme_read64(struct nvme_softc *s, uint32_t off)
{
    uint32_t lo = nvme_read32(s, off);
    uint32_t hi = nvme_read32(s, off + 4);
    return ((uint64_t)hi << 32) | lo;
}

static inline void nvme_write64(struct nvme_softc *s, uint32_t off, uint64_t v)
{
    nvme_write32(s, off, (uint32_t)(v & 0xFFFFFFFF));
    nvme_write32(s, off + 4, (uint32_t)(v >> 32));
}

/* Busy-wait loop that cannot be optimised away. */
static inline void nvme_spin(uint64_t iters)
{
    for (volatile uint64_t i = 0; i < iters; i++)
        __asm__ volatile("" ::: "memory");
}

/* Allocate `npages` physically-contiguous frames and map them uncacheable.
 * These are DMA buffers the controller reads/writes; mapping them UC keeps
 * them coherent with the device without manual cache flushes (the QEMU
 * device models read guest RAM directly and would otherwise observe a stale
 * store still sitting in the vCPU cache). */
static int nvme_dma_alloc(struct nvme_dma *d, uint64_t npages)
{
    void *phys = pmm_alloc_frames(npages);
    if (!phys)
        return -1;
    void *virt = vmm_mmap_phys((uint64_t)phys, npages, NVME_PTE_UC);
    if (!virt)
    {
        pmm_free_frames(phys, npages);
        return -1;
    }
    d->virt = virt;
    d->phys = (uint64_t)phys;
    d->npages = npages;
    memset(virt, 0, npages * PAGE_SIZE);
    return 0;
}

static void nvme_dma_free(struct nvme_dma *d)
{
    if (!d->virt)
        return;
    vmm_unmap_page((uint64_t)d->virt);
    pmm_free_frames((void *)(uintptr_t)d->phys, d->npages);
    d->virt = 0;
    d->phys = 0;
}

/* Compute (and restore) the size of PCI BAR `idx` by the write-1s/read-back
 * trick, then return the size in bytes (0 if it cannot be determined). */
static uint64_t nvme_bar_size(struct pci_device *pdev, int idx)
{
    uint8_t bus = pdev->bus, dev = pdev->dev, func = pdev->func;
    uint32_t reg = 0x10 + idx * 4;
    uint32_t orig = pci_read32(bus, dev, func, reg);
    pci_write32(bus, dev, func, reg, 0xFFFFFFFF);
    uint32_t val = pci_read32(bus, dev, func, reg);
    pci_write32(bus, dev, func, reg, orig);
    if (val == 0 || val == 0xFFFFFFFF)
        return 0;
    /* 32-bit MMIO BAR: size is encoded in bits 4..31. */
    uint64_t size = (~(uint64_t)(val & 0xFFFFFFF0)) + 1;
    return size;
}

/* Compute the doorbell register offset for a given queue + type
 * (0 = SQ tail, 1 = CQ head).  stride = 4 << DSTRD bytes (NVMe spec). */
static inline uint32_t nvme_dbl_off(struct nvme_softc *s, uint16_t qid, int type)
{
    return (uint32_t)(NVME_DOORBELL_BASE + (2 * qid + type) * s->dstrd);
}

/* ---------------------------------------------------------------------------
 * Queue command submission / completion polling
 * ------------------------------------------------------------------------- */

/* Build a command into `q`, ring its tail doorbell, then poll the completion
 * queue until the entry carrying `cid` appears.  Returns the 15-bit status
 * code (0 == success) or (uint16_t)-1 on timeout. */
static uint16_t nvme_submit(struct nvme_softc *s, struct nvme_queue *q,
                            uint8_t opcode, uint32_t nsid,
                            uint64_t prp1, uint64_t prp2,
                            uint32_t cdw10, uint32_t cdw11,
                            uint32_t cdw12, uint32_t cdw13,
                            uint32_t cdw14, uint32_t cdw15,
                            struct nvme_cqe *out)
{
    uint16_t cid = (q->qid == 0) ? s->admin_cid++ : (uint16_t)(q->sq_tail);

    volatile struct nvme_sqe *e = &q->sq[q->sq_tail];
    for (volatile int i = 0; i < (int)sizeof(*e); i++)
        ((volatile uint8_t *)e)[i] = 0;

    e->cdw0 = (uint32_t)opcode | ((uint32_t)cid << 16);  /* psdt=0 (PRP) */
    e->nsid = nsid;
    e->mptr = 0;
    e->prp1 = prp1;
    e->prp2 = prp2;
    e->cdw10 = cdw10;
    e->cdw11 = cdw11;
    e->cdw12 = cdw12;
    e->cdw13 = cdw13;
    e->cdw14 = cdw14;
    e->cdw15 = cdw15;

    uint16_t next_tail = (uint16_t)((q->sq_tail + 1) % q->size);
    q->sq_tail = next_tail;
    nvme_mb();
    nvme_write32(s, q->sq_dbl, (uint32_t)next_tail);

    /* Poll the completion queue for our entry.  We match on the command id
       (cid), which is unique for the (single) outstanding command, rather than
       relying on the phase tag alone — this tolerates any phase/head tracking
       drift between us and the controller.  Once found we re-sync cq_head and
       cq_phase so the next command starts clean.

       Phase tag: NVMe spec places it in bit 15 (status >> 15). */
    for (uint64_t guard = 0; guard < 4000000ULL; guard++)
    {
        uint32_t mask = q->size - 1;
        for (uint32_t i = 0; i < q->size; i++)
        {
            uint32_t idx = (q->cq_head + i) & mask;
            volatile struct nvme_cqe *c = &q->cq[idx];
            uint16_t phase = (uint16_t)(c->status >> 15);
            if (phase != q->cq_phase)
                continue;
            if (c->cid != cid)
                continue;
            /* Found our completion.  Consume every entry up to and including
               this one, wrapping the phase tag when the head wraps. */
            uint32_t consumed = (idx + 1) & mask;
            while (q->cq_head != consumed)
            {
                q->cq_head = (q->cq_head + 1) & mask;
                if (q->cq_head == 0)
                    q->cq_phase ^= 1;
            }
            nvme_write32(s, q->cq_dbl, (uint32_t)q->cq_head);
            if (out)
                *out = *c;
            return c->status & 0x7FFF;
        }
    }
    /* Timeout: dump diagnostic state. */
    printk("NVMe: TIMEOUT qid=%u tail=%u head=%u phase=%u\n",
           q->qid, q->sq_tail, q->cq_head, q->cq_phase);
    if (q->qid == 0)
    {
        for (uint32_t i = 0; i < q->size; i++)
        {
            volatile struct nvme_cqe *c = &q->cq[i];
            uint16_t phase = (uint16_t)(c->status & 1);
            if (c->cid == 0 && phase == 0 && c->cdw0 == 0 && c->status == 0)
                continue;
            printk("NVMe: CQ[%u] cdw0=%x status=%x cid=%x sqh=%x sqid=%x ph=%u\n",
                   i, c->cdw0, c->status, c->cid, c->sq_head, c->sq_id, phase);
        }
        printk("NVMe: CAP=%lx CC=%x CSTS=%x sq_dbl=%x cq_dbl=%x\n",
               nvme_read64(s, NVME_REG_CAP), nvme_read32(s, NVME_REG_CC),
               nvme_read32(s, NVME_REG_CSTS), q->sq_dbl, q->cq_dbl);
        printk("NVMe: ASQ=%lx ACQ=%lx SQ0TDBL=%x\n",
               nvme_read64(s, NVME_REG_ASQ), nvme_read64(s, NVME_REG_ACQ),
               nvme_read32(s, q->sq_dbl));
    }
    return (uint16_t)-1;   /* timeout */
}

/* ---------------------------------------------------------------------------
 * Admin queue setup
 * ------------------------------------------------------------------------- */

static int nvme_enable(struct nvme_softc *s)
{
    uint64_t cap = nvme_read64(s, NVME_REG_CAP);

    /* MPSMIN (bits 48..51): we only support a 4 KiB host page size. */
    uint32_t mpsmin = (uint32_t)((cap >> 48) & 0xF);
    if (mpsmin != 0)
    {
        printk("NVMe: controller requires page size %u KiB (unsupported)\n",
               1u << (mpsmin + 2));
        return -1;
    }

    /* CSS (bit 37): NVM command set must be supported. */
    if (!((cap >> 37) & 1))
    {
        printk("NVMe: NVM command set not supported\n");
        return -1;
    }

    s->dstrd = 4u << ((cap >> 32) & 0xF);   /* DSTRD is CAP bits 32..35 */

    /* 1) Disable the controller and wait until it is not ready. */
    nvme_write32(s, NVME_REG_CC, nvme_read32(s, NVME_REG_CC) & ~NVME_CC_EN);
    for (uint64_t i = 0; i < 5000000ULL; i++)
    {
        if (!(nvme_read32(s, NVME_REG_CSTS) & NVME_CSTS_RDY))
            break;
        nvme_spin(100);
    }
    if (nvme_read32(s, NVME_REG_CSTS) & NVME_CSTS_RDY)
    {
        printk("NVMe: controller did not disable\n");
        return -1;
    }

    /* 2) Program the admin queue bases + attributes (zero-based sizes). */
    nvme_write32(s, NVME_REG_AQA,
                 ((uint32_t)NVME_QUEUE_QSIZE << 16) | (uint32_t)NVME_QUEUE_QSIZE);
    nvme_write64(s, NVME_REG_ASQ, s->admin.sq_phys);
    nvme_write64(s, NVME_REG_ACQ, s->admin.cq_phys);

    /* 3) Configure + enable.  MPS=4KiB, CSS=NVM, round-robin, 64/16B entries. */
    uint32_t cc = NVME_CC_EN | NVME_CC_CSS_NVM | NVME_CC_MPS_4K |
                  NVME_CC_AMS_RR | NVME_CC_IOSQES | NVME_CC_IOCQES;
    nvme_write32(s, NVME_REG_CC, cc);

    /* 4) Wait until ready. */
    for (uint64_t i = 0; i < 5000000ULL; i++)
    {
        if (nvme_read32(s, NVME_REG_CSTS) & NVME_CSTS_RDY)
            break;
        nvme_spin(100);
    }
    if (!(nvme_read32(s, NVME_REG_CSTS) & NVME_CSTS_RDY))
    {
        printk("NVMe: controller did not become ready\n");
        return -1;
    }

    /* Mask all interrupt vectors — we poll the completion queues. */
    nvme_write32(s, NVME_REG_INTMS, 0xFFFFFFFF);
    return 0;
}

/* ---------------------------------------------------------------------------
 * Admin commands
 * ------------------------------------------------------------------------- */

static uint16_t nvme_admin_identify(struct nvme_softc *s, uint32_t cns,
                                    uint32_t nsid, uint64_t prp)
{
    return nvme_submit(s, &s->admin, NVME_ADM_IDENTIFY, nsid, prp, 0,
                       cns, 0, 0, 0, 0, 0, 0);
}

static uint16_t nvme_create_io_cq(struct nvme_softc *s, struct nvme_queue *q,
                                  uint16_t vector)
{
    /* Create I/O CQ (per QEMU/OSDev): cdw10 = (qsize-1)<<16 | qid ;
       cdw11 = (irq_vector<<16) | cq_flags (PC=1, IEN=0). */
    return nvme_submit(s, &s->admin, NVME_ADM_CREATE_CQ, 0, q->cq_phys, 0,
                       ((uint32_t)q->size - 1) << 16 | q->qid,
                       ((uint32_t)vector << 16) | (0u << 1) | 1u,
                       0, 0, 0, 0, 0);
}

static uint16_t nvme_create_io_sq(struct nvme_softc *s, struct nvme_queue *q,
                                  uint16_t cqid)
{
    /* Create I/O SQ: cdw10 = (qsize-1)<<16 | qid ; cdw11 = (cqid<<16) | PC */
    return nvme_submit(s, &s->admin, NVME_ADM_CREATE_SQ, 0, q->sq_phys, 0,
                       ((uint32_t)q->size - 1) << 16 | q->qid,
                       ((uint32_t)cqid << 16) | 1u,
                       0, 0, 0, 0, 0);
}

/* ---------------------------------------------------------------------------
 * PRP list builder
 *
 * `base` is a physically-contiguous buffer spanning `npages` 4 KiB pages.
 * Fills prp1/prp2 per the OSDev "PRP" rules:
 *   - prp1 points at the first page.
 *   - for 1 page, prp2 is unused (0).
 *   - for 2 pages, prp2 points at the second page.
 *   - for >2 pages, prp2 points at the PRP list page (in s->prplist), whose
 *     entries point at pages 1..npages-1.
 * ------------------------------------------------------------------------- */
static void nvme_build_prp(struct nvme_softc *s, uint64_t base, uint32_t npages,
                           uint64_t *prp1, uint64_t *prp2)
{
    *prp1 = base;
    if (npages <= 1)
    {
        *prp2 = 0;
        return;
    }
    if (npages == 2)
    {
        *prp2 = base + PAGE_SIZE;
        return;
    }
    *prp2 = s->prplist.phys;
    volatile uint64_t *list = (volatile uint64_t *)s->prplist.virt;
    for (uint32_t i = 1; i < npages; i++)
        list[i - 1] = base + (uint64_t)i * PAGE_SIZE;
}

/* ---------------------------------------------------------------------------
 * Block device operations (/dev/nvme0n1)
 * ------------------------------------------------------------------------- */

static long nvme_dev_read(struct device *d, uint64_t off, void *buf, size_t len)
{
    struct nvme_softc *s = (struct nvme_softc *)d->priv;
    if (!s || !s->ready)
        return -1;

    uint64_t lba = off >> s->block_shift;
    size_t remaining = len;
    size_t done = 0;
    uint8_t *p = (uint8_t *)buf;

    unsigned long flags = spin_lock_irq(&g_nvme_lock);

    while (remaining > 0)
    {
        size_t chunk = remaining > s->max_xfer ? s->max_xfer : remaining;
        uint32_t nblocks = (uint32_t)((chunk + s->block_size - 1) >> s->block_shift);
        if (s->nsze && (lba + (uint64_t)nblocks > s->nsze))
            break;   /* clamp at end of media */

        uint32_t npages = (uint32_t)((chunk + PAGE_SIZE - 1) >> PAGE_SHIFT);
        if (npages > NVME_BOUNCE_PAGES)
            npages = NVME_BOUNCE_PAGES;

        uint64_t prp1, prp2;
        nvme_build_prp(s, s->bounce.phys, npages, &prp1, &prp2);

        uint16_t st = nvme_submit(s, &s->io, NVME_IO_READ, s->nsid,
                                  prp1, prp2,
                                  (uint32_t)lba, (uint32_t)(lba >> 32),
                                  nblocks - 1, 0, 0, 0, 0);
        if (st != 0)
        {
            printk("NVMe: read failed (status 0x%x)\n", st);
            spin_unlock_irq(&g_nvme_lock, flags);
            return -1;
        }

        memcpy(p + done, s->bounce.virt, chunk);
        lba += nblocks;
        done += chunk;
        remaining -= chunk;
    }

    spin_unlock_irq(&g_nvme_lock, flags);
    return (long)done;
}

static long nvme_dev_write(struct device *d, uint64_t off, const void *buf,
                           size_t len)
{
    struct nvme_softc *s = (struct nvme_softc *)d->priv;
    if (!s || !s->ready)
        return -1;

    uint64_t lba = off >> s->block_shift;
    size_t remaining = len;
    size_t done = 0;
    const uint8_t *p = (const uint8_t *)buf;

    unsigned long flags = spin_lock_irq(&g_nvme_lock);

    while (remaining > 0)
    {
        size_t chunk = remaining > s->max_xfer ? s->max_xfer : remaining;
        uint32_t nblocks = (uint32_t)((chunk + s->block_size - 1) >> s->block_shift);
        if (s->nsze && (lba + (uint64_t)nblocks > s->nsze))
            break;

        uint32_t npages = (uint32_t)((chunk + PAGE_SIZE - 1) >> PAGE_SHIFT);
        if (npages > NVME_BOUNCE_PAGES)
            npages = NVME_BOUNCE_PAGES;

        memcpy(s->bounce.virt, p + done, chunk);

        uint64_t prp1, prp2;
        nvme_build_prp(s, s->bounce.phys, npages, &prp1, &prp2);

        uint16_t st = nvme_submit(s, &s->io, NVME_IO_WRITE, s->nsid,
                                  prp1, prp2,
                                  (uint32_t)lba, (uint32_t)(lba >> 32),
                                  nblocks - 1, 0, 0, 0, 0);
        if (st != 0)
        {
            printk("NVMe: write failed (status 0x%x)\n", st);
            spin_unlock_irq(&g_nvme_lock, flags);
            return -1;
        }

        lba += nblocks;
        done += chunk;
        remaining -= chunk;
    }

    spin_unlock_irq(&g_nvme_lock, flags);
    return (long)done;
}

/* ---------------------------------------------------------------------------
 * Controller shutdown (gentle)
 * ------------------------------------------------------------------------- */

static void nvme_shutdown(struct nvme_softc *s)
{
    if (!s->regs)
        return;

    /* Delete I/O queues first (admin commands), then shut down the controller. */
    if (s->io.sq_phys)
        nvme_submit(s, &s->admin, NVME_ADM_DELETE_SQ, 0, 0, 0, s->io.qid, 0,0,0,0,0,0);
    if (s->io.cq_phys)
        nvme_submit(s, &s->admin, NVME_ADM_DELETE_CQ, 0, 0, 0, s->io.qid, 0,0,0,0,0,0);

    uint32_t cc = nvme_read32(s, NVME_REG_CC);
    cc = (cc & ~(3u << NVME_CSTS_SHST_SHIFT)) | (1u << NVME_CSTS_SHST_SHIFT);
    nvme_write32(s, NVME_REG_CC, cc);   /* SHN = 1 (normal shutdown) */

    for (uint64_t i = 0; i < 5000000ULL; i++)
    {
        if (NVME_CSTS_SHST(nvme_read32(s, NVME_REG_CSTS)) == NVME_CSTS_SHST_CMPLT)
            break;
        nvme_spin(100);
    }
}

/* ---------------------------------------------------------------------------
 * Probe (called by the driver framework)
 * ------------------------------------------------------------------------- */

static int nvme_probe(struct pci_device *pdev)
{
    struct nvme_softc *s = &g_nvme;
    if (s->ready)
        return 0;   /* already initialised */
    memset(s, 0, sizeof(*s));

    /* --- Map BAR0 (64-bit MMIO) as uncacheable. --- */
    uint64_t bar = pci_bar_addr(pdev, 0);
    if (!bar)
    {
        printk("NVMe: BAR0 missing\n");
        return -1;
    }
    uint64_t barsz = nvme_bar_size(pdev, 0);
    uint64_t pages = (barsz + PAGE_SIZE - 1) >> PAGE_SHIFT;
    if (pages < 2)
        pages = 2;                 /* doorbells live at >= 0x1000 */
    if (pages > 64)
        pages = 64;

    void *v = vmm_mmap_phys(bar, pages, NVME_PTE_UC);
    if (!v)
    {
        printk("NVMe: failed to map BAR0 @ 0x%lx\n", bar);
        return -1;
    }
    s->regs = (volatile uint8_t *)v;
    s->regs_phys = bar;
    s->regs_pages = pages;

    /* Enable bus mastering + memory space access in the PCI command reg. */
    uint32_t cmd = pci_read32(pdev->bus, pdev->dev, pdev->func, 0x04);
    cmd |= 0x6;   /* bit1 = Memory Space, bit2 = Bus Master */
    pci_write32(pdev->bus, pdev->dev, pdev->func, 0x04, cmd);

    printk("NVMe: BAR0 @ 0x%lx (%lu pages), vend=%x dev=%x\n",
           bar, pages, (int)pdev->vendor, (int)pdev->device);

    /* --- Allocate admin + I/O queues and DMA scratch. --- */
    if (nvme_dma_alloc(&s->admin_sq, 1) ||
        nvme_dma_alloc(&s->admin_cq, 1) ||
        nvme_dma_alloc(&s->io_sq, 1) ||
        nvme_dma_alloc(&s->io_cq, 1) ||
        nvme_dma_alloc(&s->bounce, NVME_BOUNCE_PAGES) ||
        nvme_dma_alloc(&s->prplist, 1))
    {
        printk("NVMe: DMA allocation failed\n");
        goto cleanup;
    }

    s->admin.sq = (volatile struct nvme_sqe *)s->admin_sq.virt;
    s->admin.sq_phys = s->admin_sq.phys;
    s->admin.cq = (volatile struct nvme_cqe *)s->admin_cq.virt;
    s->admin.cq_phys = s->admin_cq.phys;
    s->admin.qid = 0;
    s->admin.size = NVME_QUEUE_SIZE;
    s->admin.cq_phase = 1;   /* NVMe spec: first completion after enable has phase 1 */

    s->io.sq = (volatile struct nvme_sqe *)s->io_sq.virt;
    s->io.sq_phys = s->io_sq.phys;
    s->io.cq = (volatile struct nvme_cqe *)s->io_cq.virt;
    s->io.cq_phys = s->io_cq.phys;
    s->io.qid = 1;
    s->io.size = NVME_QUEUE_SIZE;
    s->io.cq_phase = 1;   /* first completion posted with phase 1 */

    /* Door-bell offsets depend on the stride; derive it from CAP now. */
    {
        uint64_t cap = nvme_read64(s, NVME_REG_CAP);
        s->dstrd = 4u << ((cap >> 32) & 0xF);
        s->admin.sq_dbl = nvme_dbl_off(s, 0, 0);
        s->admin.cq_dbl = nvme_dbl_off(s, 0, 1);
        s->io.sq_dbl = nvme_dbl_off(s, 1, 0);
        s->io.cq_dbl = nvme_dbl_off(s, 1, 1);
    }

    /* --- Reset + enable. --- */
    if (nvme_enable(s) != 0)
        goto cleanup;

    /* --- Identify controller (needs a 4 KiB buffer). --- */
    struct nvme_dma id;
    if (nvme_dma_alloc(&id, 1))
    {
        printk("NVMe: identify buffer alloc failed\n");
        goto cleanup;
    }
    uint16_t st = nvme_admin_identify(s, NVME_IDENTIFY_CTRL, 0, id.phys);
    if (st != 0)
    {
        printk("NVMe: identify controller failed (status 0x%x)\n", st);
        nvme_dma_free(&id);
        goto cleanup;
    }

    /* MDTS (offset 77): 0 = no limit; otherwise max transfer = 2^MDTS * 4KiB. */
    uint8_t mdts = ((volatile uint8_t *)id.virt)[77];
    if (mdts == 0)
        s->max_xfer = NVME_BOUNCE_BYTES;
    else
        s->max_xfer = (1u << mdts) * PAGE_SIZE;
    if (s->max_xfer > NVME_BOUNCE_BYTES)
        s->max_xfer = NVME_BOUNCE_BYTES;
    nvme_dma_free(&id);

    /* --- Create I/O completion queue, then I/O submission queue. --- */
    st = nvme_create_io_cq(s, &s->io, 0);
    if (st != 0)
    {
        printk("NVMe: create I/O CQ failed (status 0x%x)\n", st);
        goto cleanup;
    }
    st = nvme_create_io_sq(s, &s->io, 1);   /* bound to CQ id 1 */
    if (st != 0)
    {
        printk("NVMe: create I/O SQ failed (status 0x%x)\n", st);
        goto cleanup;
    }

    /* --- Identify namespace 1. --- */
    if (nvme_dma_alloc(&id, 1))
    {
        printk("NVMe: ns identify buffer alloc failed\n");
        goto cleanup;
    }
    st = nvme_admin_identify(s, NVME_IDENTIFY_NS, 1, id.phys);
    if (st != 0)
    {
        printk("NVMe: identify namespace 1 failed (status 0x%x)\n", st);
        nvme_dma_free(&id);
        goto cleanup;
    }

    s->nsid = 1;
    s->nsze = *(volatile uint64_t *)id.virt;             /* offset 0 */
    uint8_t flbas = ((volatile uint8_t *)id.virt)[26];
    uint8_t lbaf_idx = flbas & 0xF;
    uint32_t lbaf = *(volatile uint32_t *)((volatile uint8_t *)id.virt + 128 + lbaf_idx * 4);
    uint8_t lbads = (uint8_t)((lbaf >> 16) & 0xFF);
    if (lbads == 0)
        lbads = 9;   /* 512 B default */
    s->block_size = 1u << lbads;
    s->block_shift = lbads;
    nvme_dma_free(&id);

    if (s->nsze == 0)
    {
        printk("NVMe: namespace 1 has zero size\n");
        goto cleanup;
    }

    /* --- Register the block device. --- */
    if (!device_find("nvme0n1"))
    {
        struct device *d = (struct device *)kmalloc(sizeof(struct device));
        if (d)
        {
            memset(d, 0, sizeof(*d));
            strcpy(d->name, "nvme0n1");
            d->major = 0x4E; d->minor = 0x01;   /* 'N' */
            d->type = DEV_BLOCK;
            d->priv = s;
            d->ops.read = nvme_dev_read;
            d->ops.write = nvme_dev_write;
            device_register(d);
            g_nvme_dev = d;
        }
    }

    s->ready = 1;
    printk("NVMe: ready — nsid=%u %lu blocks of %u B (max_xfer %u B)\n",
           s->nsid, (unsigned long)s->nsze, s->block_size, s->max_xfer);
    return 0;

cleanup:
    nvme_dma_free(&s->admin_sq);
    nvme_dma_free(&s->admin_cq);
    nvme_dma_free(&s->io_sq);
    nvme_dma_free(&s->io_cq);
    nvme_dma_free(&s->bounce);
    nvme_dma_free(&s->prplist);
    if (s->regs)
    {
        vmm_unmap_page((uint64_t)s->regs);
        pmm_free_frames((void *)(uintptr_t)s->regs_phys, s->regs_pages);
        s->regs = 0;
    }
    return -1;
}

/* ---------------------------------------------------------------------------
 * Module packaging (.kxt)
 * ------------------------------------------------------------------------- */

static struct driver nvme_drv = {
    .name        = "nvme",
    .vendor      = DRV_ANY,
    .device      = DRV_ANY,
    .pci_class   = 0x01,
    .pci_subclass = 0x08,
    .probe       = nvme_probe,
};

static int nvme_mod_init(void)
{
    driver_register(&nvme_drv);
    /* Probe any NVMe controller enumerated before this module loaded. */
    for (struct pci_device *p = pci_first(); p; p = p->next)
        driver_probe_pci(p);
    printk("NVMe: module initialised\n");
    return 0;
}

static void nvme_mod_exit(void)
{
    struct nvme_softc *s = &g_nvme;
    if (!s->ready)
        return;

    driver_unregister(&nvme_drv);

    /* Invalidate the /dev node before its backing data vanishes. */
    if (g_nvme_dev)
    {
        g_nvme_dev->ops.read = 0;
        g_nvme_dev->ops.write = 0;
        g_nvme_dev->priv = 0;
        g_nvme_dev = 0;
    }

    nvme_shutdown(s);

    nvme_dma_free(&s->admin_sq);
    nvme_dma_free(&s->admin_cq);
    nvme_dma_free(&s->io_sq);
    nvme_dma_free(&s->io_cq);
    nvme_dma_free(&s->bounce);
    nvme_dma_free(&s->prplist);
    if (s->regs)
    {
        vmm_unmap_page((uint64_t)s->regs);
        pmm_free_frames((void *)(uintptr_t)s->regs_phys, s->regs_pages);
        s->regs = 0;
    }
    s->ready = 0;
    printk("NVMe: module unloaded\n");
}

MODULE_INIT(nvme_mod_init);
MODULE_EXIT(nvme_mod_exit);
