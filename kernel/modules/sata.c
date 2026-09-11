/* ============================================================================
 * SATA (AHCI) driver (.kxt loadable kernel module)
 *
 * Implements a minimal but functional Serial ATA host driver using the AHCI
 * (Advance Host Controller Interface) programming model, following the OSDev
 * "AHCI" guide and the Intel AHCI 1.3 specification.  It:
 *
 *   1. Probes the PCI function (class 0x01, subclass 0x06), enables bus
 *      mastering + memory space, and maps the ABAR (BAR5) register space as
 *      uncacheable (UC) memory — required for MMIO device registers.
 *   2. Takes ownership from the BIOS if required, enables AHCI mode (GHC.AE)
 *      and performs a global HBA reset.
 *   3. For every implemented port with an ATA (SATA) drive attached, it
 *      allocates the command list, received-FIS area and command tables,
 *      rebases the port memory, and issues an IDENTIFY command to learn the
 *      sector count and model string.
 *   4. Exposes each drive as a block device (/dev/sataN) via the driver
 *      framework's dev_ops read/write, using READ/WRITE DMA EXT with PRDTs.
 *
 * DESIGN NOTE — polled completions:
 *   The kernel has no generic per-vector IRQ registration for modules, so
 *   this driver runs the ports in *polled* mode, exactly like the NVMe and
 *   e1000 modules: port interrupts stay masked and commands are considered
 *   complete when the controller clears the corresponding bit in PxCI (and no
 *   Task File Error was latched in PxIS).  This is fully correct and avoids
 *   any dependency on IRQ routing.
 *
 * Memory ordering:
 *   Command lists, FIS areas, command tables and bounce buffers are all mapped
 *   uncacheable (UC).  UC accesses are strongly ordered, so a PxCI write (also
 *   UC) is never observed by the controller before the FIS/PRDT stores that
 *   precede it — no fence is required (the same rationale as the NVMe module's
 *   UC DMA buffers).
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

/* ---- Generic host controller register offsets (ABAR) ---- */
#define HBA_CAP_OFF   0x00   /* Host capabilities                       */
#define HBA_GHC_OFF   0x04   /* Global host control                     */
#define HBA_IS_OFF    0x08   /* Interrupt status                        */
#define HBA_PI_OFF    0x0C   /* Port implemented                        */
#define HBA_VS_OFF    0x10   /* Version                                 */
#define HBA_CAP2_OFF  0x24   /* Host capabilities extended              */
#define HBA_BOHC_OFF  0x28   /* BIOS/OS handoff control and status      */

/* GHC bits. */
#define HBA_GHC_AE    (1u << 31)   /* AHCI enable                        */
#define HBA_GHC_IE    (1u << 1)    /* Interrupt enable                   */
#define HBA_GHC_HR    (1u << 0)    /* HBA reset                          */

/* CAP2 bits. */
#define HBA_CAP2_BOS  (1u << 5)    /* BIOS/OS handoff supported          */

/* BOHC bits. */
#define HBA_BOHC_OOS  (1u << 3)    /* OS owns semaphore                  */
#define HBA_BOHC_BB   (1u << 1)    /* BIOS busy                          */
#define HBA_BOHC_BOS  (1u << 0)    /* BIOS OS handoff control            */

/* ---- Port register offsets (relative to Px = ABAR + 0x100 + n*0x80) ---- */
#define PX_CLB    0x00   /* Command list base (1K aligned)               */
#define PX_CLBU   0x04   /* Command list base upper 32 bits              */
#define PX_FB     0x08   /* FIS base (256 aligned)                       */
#define PX_FBU    0x0C   /* FIS base upper 32 bits                       */
#define PX_IS     0x10   /* Interrupt status                             */
#define PX_IE     0x14   /* Interrupt enable                             */
#define PX_CMD    0x18   /* Command and status                           */
#define PX_TFD    0x20   /* Task file data                               */
#define PX_SIG    0x24   /* Signature                                    */
#define PX_SSTS   0x28   /* SATA status (SCR0: SStatus)                  */
#define PX_SCTL   0x2C   /* SATA control (SCR2: SControl)                */
#define PX_SERR   0x30   /* SATA error (SCR1: SError)                    */
#define PX_SACT   0x34   /* SATA active (SCR3: SActive)                  */
#define PX_CI     0x38   /* Command issue                                */
#define PX_SNTF   0x3C   /* SATA notification (SCR4: SNotification)      */

/* PxCMD bits. */
#define HBA_PxCMD_ST    0x0001   /* Start                                 */
#define HBA_PxCMD_FRE   0x0010   /* FIS receive enable                    */
#define HBA_PxCMD_FR    0x4000   /* FIS receive running                   */
#define HBA_PxCMD_CR    0x8000   /* Command list running                  */

/* PxIS bits. */
#define HBA_PxIS_TFES   (1u << 30)   /* Task file error status            */

/* Device signatures (read from PxSIG). */
#define SATA_SIG_ATA    0x00000101   /* SATA drive                        */
#define SATA_SIG_ATAPI  0xEB140101   /* SATAPI drive                      */
#define SATA_SIG_SEMB   0xC33C0101   /* Enclosure management bridge       */
#define SATA_SIG_PM     0x96690101   /* Port multiplier                   */

/* ATA commands (SATA uses the ATA command set). */
#define ATA_CMD_IDENTIFY      0xEC
#define ATA_CMD_READ_DMA_EX   0x25   /* LBA48 read                        */
#define ATA_CMD_WRITE_DMA_EX  0x35   /* LBA48 write                       */

/* FIS types. */
#define FIS_TYPE_REG_H2D 0x27

/* Task file status bits used by the busy-wait before issuing. */
#define ATA_DEV_BUSY 0x80
#define ATA_DEV_DRQ  0x08

/* Page-table bits for the uncacheable DMA/MMIO mappings. */
#define SATA_PTE_UC (MMU_WRITE | MMU_UNCACHED)

#define SATA_MAX_PORTS 32
#define SATA_SLOTS     32              /* command slots per port           */
#define SATA_MAX_PRDT  8               /* PRDT entries reserved per table  */
#define SATA_CT_STRIDE 256             /* bytes per command table          */
#define SATA_CT_BYTES  (SATA_SLOTS * SATA_CT_STRIDE)   /* 8 KiB/port       */
#define SATA_BOUNCE_PAGES 8            /* 32 KiB bounce buffer per port    */
#define SATA_BOUNCE_BYTES (SATA_BOUNCE_PAGES * PAGE_SIZE)

/* ---------------------------------------------------------------------------
 * Data structures (see the OSDev "Data structures" section)
 * ------------------------------------------------------------------------- */

/* Register FIS — Host to Device (20 bytes, cfl = 5 dwords). */
struct fis_reg_h2d {
    uint8_t fis_type;
    uint8_t pmport:4;
    uint8_t rsv0:3;
    uint8_t c:1;
    uint8_t command;
    uint8_t featurel;
    uint8_t lba0;
    uint8_t lba1;
    uint8_t lba2;
    uint8_t device;
    uint8_t lba3;
    uint8_t lba4;
    uint8_t lba5;
    uint8_t featureh;
    uint8_t countl;
    uint8_t counth;
    uint8_t icc;
    uint8_t control;
    uint8_t rsv1[4];
} __attribute__((packed));

/* Command header (32 bytes). */
struct hba_cmd_header {
    uint8_t cfl:5;
    uint8_t a:1;
    uint8_t w:1;
    uint8_t p:1;
    uint8_t r:1;
    uint8_t b:1;
    uint8_t c:1;
    uint8_t rsv0:1;
    uint8_t pmp:4;
    uint16_t prdtl;
    uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t rsv1[4];
} __attribute__((packed));

/* Physical region descriptor table entry (16 bytes). */
struct hba_prdt {
    uint32_t dba;
    uint32_t dbau;
    uint32_t rsv0;
    uint32_t dbc:22;
    uint32_t rsv1:9;
    uint32_t i:1;
} __attribute__((packed));

/* Command table (256 bytes: 64 cfis + 16 acmd + 48 rsv + 8*16 prdt). */
struct hba_cmd_tbl {
    uint8_t cfis[64];
    uint8_t acmd[16];
    uint8_t rsv[48];
    struct hba_prdt prdt[SATA_MAX_PRDT];
} __attribute__((packed));

/* One physically-contiguous DMA allocation (free on unload). */
struct sata_dma {
    void    *virt;
    uint64_t phys;
    uint64_t npages;
};

struct sata_port {
    volatile uint8_t *regs;         /* port register block (Px base)       */
    int present;
    int index;
    uint32_t nslots;
    uint64_t sectors;               /* total 512-byte sectors              */
    char model[41];
    struct sata_dma cl;             /* command list (1 page)               */
    struct sata_dma fis;            /* received FIS (1 page)               */
    struct sata_dma ct;             /* command tables (2 pages, 8 KiB)     */
    struct sata_dma buf;            /* bounce buffer for data transfers    */
    struct device *dev;             /* registered block device, or 0       */
};

struct sata_softc {
    volatile uint8_t *regs;         /* virtual ABAR base (BAR5, UC)        */
    uint64_t regs_phys;
    uint64_t regs_pages;
    uint32_t nports;
    uint32_t slots;
    uint32_t pi;
    struct sata_port ports[SATA_MAX_PORTS];
    int ready;
};

static struct sata_softc g_sata;
static spinlock_t g_sata_lock = SPINLOCK_INIT;

/* ---------------------------------------------------------------------------
 * MMIO helpers
 * ------------------------------------------------------------------------- */

static inline uint32_t sata_read32(struct sata_softc *s, uint32_t off)
{
    return *(volatile uint32_t *)(s->regs + off);
}

static inline void sata_write32(struct sata_softc *s, uint32_t off, uint32_t v)
{
    *(volatile uint32_t *)(s->regs + off) = v;
}

static inline uint32_t sata_port_read32(struct sata_port *p, uint32_t off)
{
    return *(volatile uint32_t *)(p->regs + off);
}

static inline void sata_port_write32(struct sata_port *p, uint32_t off, uint32_t v)
{
    *(volatile uint32_t *)(p->regs + off) = v;
}

/* Busy-wait loop that cannot be optimised away. */
static inline void sata_spin(uint64_t iters)
{
    for (volatile uint64_t i = 0; i < iters; i++)
        __asm__ volatile("" ::: "memory");
}

/* ---------------------------------------------------------------------------
 * DMA allocation
 * ------------------------------------------------------------------------- */

static int sata_dma_alloc(struct sata_dma *d, uint64_t npages)
{
    void *phys = pmm_alloc_frames(npages);
    if (!phys)
        return -1;
    void *virt = vmm_mmap_phys((uint64_t)phys, npages, SATA_PTE_UC);
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

static void sata_dma_free(struct sata_dma *d)
{
    if (!d->virt)
        return;
    for (uint64_t i = 0; i < d->npages; i++)
        vmm_unmap_page((uint64_t)d->virt + i * PAGE_SIZE);
    pmm_free_frames((void *)(uintptr_t)d->phys, d->npages);
    d->virt = 0;
    d->phys = 0;
}

static void sata_port_dma_free(struct sata_port *p)
{
    sata_dma_free(&p->cl);
    sata_dma_free(&p->fis);
    sata_dma_free(&p->ct);
    sata_dma_free(&p->buf);
}

/* Compute the size of PCI BAR `idx` by the write-1s/read-back trick, then
 * return the size in bytes (0 if it cannot be determined). */
static uint64_t sata_bar_size(struct pci_device *pdev, int idx)
{
    uint8_t bus = pdev->bus, dev = pdev->dev, func = pdev->func;
    uint32_t reg = 0x10 + idx * 4;
    uint32_t orig = pci_read32(bus, dev, func, reg);
    pci_write32(bus, dev, func, reg, 0xFFFFFFFF);
    uint32_t val = pci_read32(bus, dev, func, reg);
    pci_write32(bus, dev, func, reg, orig);
    if (val == 0 || val == 0xFFFFFFFF)
        return 0;
    uint64_t size = (~(uint64_t)(val & 0xFFFFFFF0)) + 1;
    return size;
}

/* ---------------------------------------------------------------------------
 * Port command engine control
 * ------------------------------------------------------------------------- */

static void sata_start_cmd(struct sata_port *p)
{
    while (sata_port_read32(p, PX_CMD) & HBA_PxCMD_CR)
        ;
    uint32_t cmd = sata_port_read32(p, PX_CMD);
    cmd |= HBA_PxCMD_FRE;                    /* enable FIS reception */
    sata_port_write32(p, PX_CMD, cmd);
    cmd = sata_port_read32(p, PX_CMD);
    cmd |= HBA_PxCMD_ST;                     /* start command engine */
    sata_port_write32(p, PX_CMD, cmd);
}

static void sata_stop_cmd(struct sata_port *p)
{
    uint32_t cmd = sata_port_read32(p, PX_CMD);
    cmd &= ~HBA_PxCMD_ST;
    sata_port_write32(p, PX_CMD, cmd);
    cmd = sata_port_read32(p, PX_CMD);
    cmd &= ~HBA_PxCMD_FRE;
    sata_port_write32(p, PX_CMD, cmd);
    for (uint32_t i = 0; i < 1000000; i++)
    {
        uint32_t c = sata_port_read32(p, PX_CMD);
        if (!(c & HBA_PxCMD_FR) && !(c & HBA_PxCMD_CR))
            break;
    }
}

/* Wait for the SATA link to come up (SStatus DET=3, IPM=1). */
static int sata_port_wait_link(struct sata_port *p)
{
    uint32_t det0 = 0;
    for (uint32_t i = 0; i < 2000000; i++)
    {
        uint32_t ssts = sata_port_read32(p, PX_SSTS);
        uint8_t det = (uint8_t)(ssts & 0x0F);
        uint8_t ipm = (uint8_t)((ssts >> 8) & 0x0F);
        if (det == 3 && ipm == 1)
            return 0;
        if (det == 0 && ++det0 > 1000)
            return -1;                       /* no device on this port */
    }
    return -1;
}

/* Rebase the port memory: command list, received FIS and command tables. */
static void sata_port_rebase(struct sata_port *p)
{
    sata_stop_cmd(p);

    /* Command list offset: 1K-aligned, 32*32 = 1K bytes per port. */
    sata_port_write32(p, PX_CLB, (uint32_t)p->cl.phys);
    sata_port_write32(p, PX_CLBU, 0);
    memset(p->cl.virt, 0, 1024);

    /* FIS area: 256-byte aligned, 256 bytes per port. */
    sata_port_write32(p, PX_FB, (uint32_t)p->fis.phys);
    sata_port_write32(p, PX_FBU, 0);
    memset(p->fis.virt, 0, 256);

    /* Command tables: 8 KiB per port, one 256-byte table per slot. */
    struct hba_cmd_header *ch = (struct hba_cmd_header *)p->cl.virt;
    memset(p->ct.virt, 0, SATA_CT_BYTES);
    for (uint32_t i = 0; i < p->nslots; i++)
    {
        ch[i].prdtl = SATA_MAX_PRDT;
        ch[i].ctba = (uint32_t)(p->ct.phys + i * SATA_CT_STRIDE);
        ch[i].ctbau = 0;
    }

    sata_start_cmd(p);
}

/* Find a free command slot (not set in PxSACT or PxCI). -1 if none. */
static int sata_find_slot(struct sata_port *p)
{
    uint32_t slots = sata_port_read32(p, PX_SACT) | sata_port_read32(p, PX_CI);
    for (uint32_t i = 0; i < p->nslots; i++)
        if (!(slots & (1u << i)))
            return (int)i;
    return -1;
}

/* ---------------------------------------------------------------------------
 * Command issue (polled completion)
 * ------------------------------------------------------------------------- */

/* Issue one ATA command that transfers `nblocks` sectors between the bounce
 * buffer and the drive via the DMA engine.  Returns 0 on success. */
static int sata_cmd(struct sata_port *p, uint8_t cmd, uint64_t lba,
                    uint16_t nblocks, int write)
{
    int slot = sata_find_slot(p);
    if (slot < 0)
    {
        printk("SATA: port %u no free command slot\n", (unsigned int)p->index);
        return -1;
    }

    struct hba_cmd_header *ch = (struct hba_cmd_header *)p->cl.virt;
    struct hba_cmd_tbl *ct =
        (struct hba_cmd_tbl *)(p->ct.virt + slot * SATA_CT_STRIDE);
    memset(ct, 0, SATA_CT_STRIDE);

    ch[slot].cfl = sizeof(struct fis_reg_h2d) / sizeof(uint32_t);   /* 5 */
    ch[slot].w = write ? 1 : 0;      /* 0: D2H read, 1: H2D write */
    ch[slot].prdtl = 1;

    ct->prdt[0].dba = (uint32_t)p->buf.phys;
    ct->prdt[0].dbau = 0;
    ct->prdt[0].dbc = (uint32_t)(nblocks * 512) - 1;   /* value is count-1 */
    ct->prdt[0].i = 1;

    struct fis_reg_h2d *f = (struct fis_reg_h2d *)ct->cfis;
    f->fis_type = FIS_TYPE_REG_H2D;
    f->c = 1;                                        /* write command reg */
    f->command = cmd;
    f->lba0 = (uint8_t)(lba & 0xFF);
    f->lba1 = (uint8_t)((lba >> 8) & 0xFF);
    f->lba2 = (uint8_t)((lba >> 16) & 0xFF);
    f->device = 1 << 6;                              /* LBA mode, master */
    f->lba3 = (uint8_t)((lba >> 24) & 0xFF);
    f->lba4 = (uint8_t)((lba >> 32) & 0xFF);
    f->lba5 = (uint8_t)((lba >> 40) & 0xFF);
    f->countl = (uint8_t)(nblocks & 0xFF);
    f->counth = (uint8_t)((nblocks >> 8) & 0xFF);

    /* Clear pending interrupt bits, then wait until the port is not busy. */
    sata_port_write32(p, PX_IS, 0xFFFFFFFF);
    uint32_t spin = 0;
    while ((sata_port_read32(p, PX_TFD) & (ATA_DEV_BUSY | ATA_DEV_DRQ)) &&
           spin < 1000000)
        spin++;
    if (spin >= 1000000)
    {
        printk("SATA: port %u hung before issue (tfd=%x)\n",
               (unsigned int)p->index, sata_port_read32(p, PX_TFD));
        return -1;
    }

    sata_port_write32(p, PX_CI, 1u << slot);         /* issue command */

    /* Wait for completion: the controller clears our bit in PxCI. */
    uint64_t guard;
    for (guard = 0; guard < 5000000; guard++)
    {
        if ((sata_port_read32(p, PX_CI) & (1u << slot)) == 0)
            break;
        if (sata_port_read32(p, PX_IS) & HBA_PxIS_TFES)
        {
            printk("SATA: port %u command error (cmd=%x tfd=%x serr=%x)\n",
                   (unsigned int)p->index, cmd, sata_port_read32(p, PX_TFD),
                   sata_port_read32(p, PX_SERR));
            return -1;
        }
    }
    if (guard >= 5000000)
    {
        printk("SATA: port %u command timeout (cmd=%x ci=%x tfd=%x)\n",
               (unsigned int)p->index, cmd, sata_port_read32(p, PX_CI),
               sata_port_read32(p, PX_TFD));
        return -1;
    }
    if (sata_port_read32(p, PX_IS) & HBA_PxIS_TFES)
    {
        printk("SATA: port %u command error (cmd=%x tfd=%x serr=%x)\n",
               (unsigned int)p->index, cmd, sata_port_read32(p, PX_TFD),
               sata_port_read32(p, PX_SERR));
        return -1;
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * Block device operations (/dev/sataN)
 * ------------------------------------------------------------------------- */

static long sata_dev_read(struct device *dev, uint64_t off, void *buf, size_t len)
{
    struct sata_port *p = (struct sata_port *)dev->priv;
    if (!p || !p->present)
        return -1;

    uint64_t lba = off >> 9;
    size_t remaining = len;
    size_t done = 0;
    uint8_t *dst = (uint8_t *)buf;

    unsigned long flags = spin_lock_irq(&g_sata_lock);

    while (remaining > 0)
    {
        size_t chunk = remaining > SATA_BOUNCE_BYTES ? SATA_BOUNCE_BYTES : remaining;
        uint32_t nblocks = (uint32_t)((chunk + 511) >> 9);
        if (p->sectors && (lba + nblocks > p->sectors))
            break;                              /* clamp at end of media */
        if (sata_cmd(p, ATA_CMD_READ_DMA_EX, lba, (uint16_t)nblocks, 0) != 0)
        {
            printk("SATA: read failed at lba %lu\n", (unsigned long)lba);
            spin_unlock_irq(&g_sata_lock, flags);
            return -1;
        }
        memcpy(dst + done, p->buf.virt, chunk);
        lba += nblocks;
        done += chunk;
        remaining -= chunk;
    }

    spin_unlock_irq(&g_sata_lock, flags);
    return (long)done;
}

static long sata_dev_write(struct device *dev, uint64_t off, const void *buf,
                           size_t len)
{
    struct sata_port *p = (struct sata_port *)dev->priv;
    if (!p || !p->present)
        return -1;

    uint64_t lba = off >> 9;
    size_t remaining = len;
    size_t done = 0;
    const uint8_t *src = (const uint8_t *)buf;

    unsigned long flags = spin_lock_irq(&g_sata_lock);

    while (remaining > 0)
    {
        size_t chunk = remaining > SATA_BOUNCE_BYTES ? SATA_BOUNCE_BYTES : remaining;
        uint32_t nblocks = (uint32_t)((chunk + 511) >> 9);
        if (p->sectors && (lba + nblocks > p->sectors))
            break;
        memcpy(p->buf.virt, src + done, chunk);
        if (sata_cmd(p, ATA_CMD_WRITE_DMA_EX, lba, (uint16_t)nblocks, 1) != 0)
        {
            printk("SATA: write failed at lba %lu\n", (unsigned long)lba);
            spin_unlock_irq(&g_sata_lock, flags);
            return -1;
        }
        lba += nblocks;
        done += chunk;
        remaining -= chunk;
    }

    spin_unlock_irq(&g_sata_lock, flags);
    return (long)done;
}

/* ---------------------------------------------------------------------------
 * Port probing / IDENTIFY
 * ------------------------------------------------------------------------- */

static void sata_make_name(char *dst, uint32_t idx)
{
    const char *pre = "sata";
    int n = 0;
    while (*pre)
        dst[n++] = *pre++;
    if (idx < 10)
        dst[n++] = (char)('0' + idx);
    else
    {
        dst[n++] = (char)('0' + idx / 10);
        dst[n++] = (char)('0' + idx % 10);
    }
    dst[n] = 0;
}

/* Send the ATA IDENTIFY command and fill in p->sectors and p->model. */
static int sata_identify(struct sata_port *p)
{
    int slot = sata_find_slot(p);
    if (slot < 0)
        return -1;

    struct hba_cmd_header *ch = (struct hba_cmd_header *)p->cl.virt;
    struct hba_cmd_tbl *ct =
        (struct hba_cmd_tbl *)(p->ct.virt + slot * SATA_CT_STRIDE);
    memset(ct, 0, SATA_CT_STRIDE);

    ch[slot].cfl = sizeof(struct fis_reg_h2d) / sizeof(uint32_t);
    ch[slot].w = 0;
    ch[slot].prdtl = 1;

    ct->prdt[0].dba = (uint32_t)p->buf.phys;
    ct->prdt[0].dbau = 0;
    ct->prdt[0].dbc = 512 - 1;
    ct->prdt[0].i = 1;

    struct fis_reg_h2d *f = (struct fis_reg_h2d *)ct->cfis;
    f->fis_type = FIS_TYPE_REG_H2D;
    f->c = 1;
    f->command = ATA_CMD_IDENTIFY;
    f->device = 0;

    /* Issue + poll (mirrors sata_cmd but with a fixed 512-byte PRDT). */
    sata_port_write32(p, PX_IS, 0xFFFFFFFF);
    uint32_t spin = 0;
    while ((sata_port_read32(p, PX_TFD) & (ATA_DEV_BUSY | ATA_DEV_DRQ)) &&
           spin < 1000000)
        spin++;
    if (spin >= 1000000)
        return -1;

    sata_port_write32(p, PX_CI, 1u << slot);

    uint64_t guard;
    for (guard = 0; guard < 5000000; guard++)
    {
        if ((sata_port_read32(p, PX_CI) & (1u << slot)) == 0)
            break;
        if (sata_port_read32(p, PX_IS) & HBA_PxIS_TFES)
            return -1;
    }
    if (guard >= 5000000)
        return -1;
    if (sata_port_read32(p, PX_IS) & HBA_PxIS_TFES)
        return -1;
    return 0;
}

static int sata_port_init(struct sata_softc *s, uint32_t n)
{
    struct sata_port *p = &s->ports[n];
    memset(p, 0, sizeof(*p));
    p->index = (int)n;
    p->regs = s->regs + (0x100 + n * 0x80);
    p->nslots = s->slots;
    p->present = 0;

    /* Wait for the link, then check what kind of device is attached. */
    if (sata_port_wait_link(p) != 0)
    {
        printk("SATA: port %u link down (ssts=%x)\n", (unsigned int)n,
               sata_port_read32(p, PX_SSTS));
        return -1;
    }
    uint32_t sig = sata_port_read32(p, PX_SIG);
    if (sig != SATA_SIG_ATA)
    {
        printk("SATA: port %u signature %x (not an ATA drive, ssts=%x)\n",
               (unsigned int)n, sig, sata_port_read32(p, PX_SSTS));
        return -1;
    }

    /* Allocate the port's DMA memory. */
    if (sata_dma_alloc(&p->cl, 1) ||
        sata_dma_alloc(&p->fis, 1) ||
        sata_dma_alloc(&p->ct, 2) ||
        sata_dma_alloc(&p->buf, SATA_BOUNCE_PAGES))
    {
        printk("SATA: port %u DMA allocation failed\n", (unsigned int)n);
        sata_port_dma_free(p);
        return -1;
    }

    sata_port_rebase(p);

    if (sata_identify(p) != 0)
    {
        printk("SATA: port %u IDENTIFY failed\n", (unsigned int)n);
        sata_port_dma_free(p);
        return -1;
    }

    /* Parse the IDENTIFY data (512 bytes of little-endian words). */
    uint16_t *id = (uint16_t *)p->buf.virt;
    if (id[83] & 0x0400)                        /* word 83 bit 10: LBA48 */
        p->sectors = (uint64_t)id[100] | ((uint64_t)id[101] << 16) |
                     ((uint64_t)id[102] << 32) | ((uint64_t)id[103] << 48);
    else
        p->sectors = (uint64_t)id[60] | ((uint64_t)id[61] << 16);

    /* Model number: words 27..46, high byte first, padded with spaces. */
    for (int i = 0; i < 20; i++)
    {
        uint16_t w = id[27 + i];
        p->model[i * 2] = (char)(w >> 8);
        p->model[i * 2 + 1] = (char)(w & 0xFF);
    }
    p->model[40] = 0;
    for (int i = 39; i >= 0; i--)
    {
        if (p->model[i] == ' ')
            p->model[i] = 0;
        else if (p->model[i])
            break;
    }

    p->present = 1;

    /* Register the block device. */
    char devname[32];
    sata_make_name(devname, n);
    if (!device_find(devname))
    {
        struct device *d = (struct device *)kmalloc(sizeof(struct device));
        if (d)
        {
            memset(d, 0, sizeof(*d));
            strcpy(d->name, devname);
            d->major = 0x53; d->minor = (uint32_t)n;   /* 'S' */
            d->type = DEV_BLOCK;
            d->priv = p;
            d->ops.read = sata_dev_read;
            d->ops.write = sata_dev_write;
            device_register(d);
            p->dev = d;
        }
    }
    printk("SATA: /dev/%s — port %u, %lu sectors [%s]\n",
           devname, (unsigned int)n, (unsigned long)p->sectors, p->model);
    return 0;
}

/* ---------------------------------------------------------------------------
 * Probe (called by the driver framework)
 * ------------------------------------------------------------------------- */

static int sata_probe(struct pci_device *pdev)
{
    struct sata_softc *s = &g_sata;
    if (s->ready)
        return 0;                               /* already initialised */
    memset(s, 0, sizeof(*s));

    /* BAR5 must be an MMIO BAR (an IO-port BAR means legacy IDE mode). */
    uint32_t bar5_raw = pci_read32(pdev->bus, pdev->dev, pdev->func, 0x24);
    if (bar5_raw & 1)
    {
        printk("SATA: ABAR is an IO-port BAR (legacy mode); use the ide driver\n");
        return -1;
    }
    uint64_t bar = pci_bar_addr(pdev, 5);
    if (!bar)
    {
        printk("SATA: no ABAR (BAR5)\n");
        return -1;
    }

    uint64_t barsz = sata_bar_size(pdev, 5);
    uint64_t pages = (barsz + PAGE_SIZE - 1) >> PAGE_SHIFT;
    if (pages < 2)
        pages = 2;
    if (pages > 64)
        pages = 64;

    void *v = vmm_mmap_phys(bar, pages, SATA_PTE_UC);
    if (!v)
    {
        printk("SATA: failed to map ABAR @ 0x%lx\n", bar);
        return -1;
    }
    s->regs = (volatile uint8_t *)v;
    s->regs_phys = bar;
    s->regs_pages = pages;

    /* Enable memory space + bus mastering in the PCI command register. */
    uint32_t cmd = pci_read32(pdev->bus, pdev->dev, pdev->func, 0x04);
    cmd |= 0x6;                                 /* bit1 MEM, bit2 BM */
    pci_write32(pdev->bus, pdev->dev, pdev->func, 0x04, cmd);

    printk("SATA: ABAR @ 0x%lx (%lu pages), vend=%x dev=%x prog-if=%x\n",
           bar, pages, (int)pdev->vendor, (int)pdev->device, (int)pdev->prog_if);

    /* Enable AHCI mode so the port registers are accessible. */
    sata_write32(s, HBA_GHC_OFF, sata_read32(s, HBA_GHC_OFF) | HBA_GHC_AE);

    /* BIOS/OS handoff (only if the controller advertises support). */
    if (sata_read32(s, HBA_CAP2_OFF) & HBA_CAP2_BOS)
    {
        uint32_t bohc = sata_read32(s, HBA_BOHC_OFF);
        if (bohc & (HBA_BOHC_BOS | HBA_BOHC_BB))
        {
            sata_write32(s, HBA_BOHC_OFF, bohc | HBA_BOHC_BOS);
            uint32_t h = 0;
            while ((sata_read32(s, HBA_BOHC_OFF) & HBA_BOHC_BB) && h < 100000)
                h++;
            sata_write32(s, HBA_BOHC_OFF,
                         sata_read32(s, HBA_BOHC_OFF) | HBA_BOHC_OOS);
        }
    }

    /* NOTE: we deliberately do NOT perform a global HBA reset (GHC.HR).
       Firmware (OVMF/BIOS) has already brought the controller into a minimal
       workable state and latched each port's device signature; resetting the
       whole HBA here would tear the SATA links down and force a re-negotiation
       (which also loses the signature that the port detection relies on).
       We only ensure AHCI mode is enabled and take ownership if required. */

    /* Read capabilities and the implemented-port bitmap. */
    uint32_t cap = sata_read32(s, HBA_CAP_OFF);
    uint32_t vs = sata_read32(s, HBA_VS_OFF);
    uint32_t pi = sata_read32(s, HBA_PI_OFF);
    uint32_t nports = (cap & 0x1F) + 1;         /* NP: number of ports */
    uint32_t slots = ((cap >> 8) & 0x1F) + 1;   /* NCS: slots per port */
    if (nports > SATA_MAX_PORTS)
        nports = SATA_MAX_PORTS;

    s->nports = nports;
    s->slots = slots;
    s->pi = pi;
    printk("SATA: HBA v%x.%x, %u ports, %u slots/port, PI=%x\n",
           (int)((vs >> 16) & 0xFF), (int)(vs & 0xFF), nports, slots, pi);

    int disks = 0;
    for (uint32_t n = 0; n < 32; n++)
    {
        if (!(pi & (1u << n)))
            continue;
        if (n >= nports)
            break;
        if (sata_port_init(s, n) == 0)
            disks++;
    }

    /* Boot-time self-test: read sector 0 of the first disk and print the MBR
       signature, proving the DMA read path without any userspace interaction. */
    if (disks > 0)
    {
        struct sata_port *p0 = 0;
        for (uint32_t n = 0; n < SATA_MAX_PORTS; n++)
            if (s->ports[n].present)
            {
                p0 = &s->ports[n];
                break;
            }
        if (p0)
        {
            if (sata_cmd(p0, ATA_CMD_READ_DMA_EX, 0, 1, 0) == 0)
            {
                uint8_t *mbr = (uint8_t *)p0->buf.virt;
                uint16_t sig = (uint16_t)(mbr[510] | (mbr[511] << 8));
                printk("SATA: self-test read LBA0 OK (MBR sig=%x)\n", sig);
            }
            else
            {
                printk("SATA: self-test read LBA0 FAILED\n");
            }

            /* Write/read-back round-trip on LBA 1 to validate the write path. */
            memset(p0->buf.virt, 0, 512);
            for (int i = 0; i < 512; i++)
                ((uint8_t *)p0->buf.virt)[i] = (uint8_t)(0xA0 + (i & 0x0F));
            if (sata_cmd(p0, ATA_CMD_WRITE_DMA_EX, 1, 1, 1) == 0 &&
                sata_cmd(p0, ATA_CMD_READ_DMA_EX, 1, 1, 0) == 0)
            {
                int ok = 1;
                for (int i = 0; i < 512; i++)
                {
                    if (((uint8_t *)p0->buf.virt)[i] != (uint8_t)(0xA0 + (i & 0x0F)))
                    {
                        ok = 0;
                        break;
                    }
                }
                printk("SATA: self-test write/read LBA1 %s\n", ok ? "OK" : "MISMATCH");
            }
            else
            {
                printk("SATA: self-test write/read LBA1 FAILED\n");
            }
        }
    }

    s->ready = 1;
    printk("SATA: driver ready — %d disk(s)\n", disks);
    return 0;
}

/* ---------------------------------------------------------------------------
 * Packaging: loadable .kxt module -or- built-in kernel driver.
 *
 * With AXIOME_BUILTIN_DRIVER (set by kernel/Makefile) this file is linked
 * into kernel.elf: driver_init() calls sata_driver_init() to register, and
 * the framework's single probe pass binds the controller. Unload support
 * is compiled out — built-ins cannot be unloaded.
 * Without it, this builds as sata.kxt: the loader calls module_init().
 * ------------------------------------------------------------------------- */

static struct driver sata_drv = {
    .name        = "sata",
    .vendor      = DRV_ANY,
    .device      = DRV_ANY,
    .pci_class   = 0x01,
    .pci_subclass = 0x06,
    .probe       = sata_probe,
};

#ifdef AXIOME_BUILTIN_DRIVER
void sata_driver_init(void)
{
    driver_register(&sata_drv);
}
#else
static int sata_mod_init(void)
{
    driver_register(&sata_drv);
    /* Probe any AHCI controller enumerated before this module loaded.
       Already-owned devices are skipped by the framework. */
    driver_probe_all();
    printk("SATA: module initialised\n");
    return 0;
}
#endif

#ifndef AXIOME_BUILTIN_DRIVER
static void sata_mod_exit(void)
{
    struct sata_softc *s = &g_sata;
    if (!s->ready)
        return;

    driver_unregister(&sata_drv);

    for (uint32_t n = 0; n < SATA_MAX_PORTS; n++)
    {
        struct sata_port *p = &s->ports[n];
        if (!p->present)
            continue;
        if (p->dev)
        {
            p->dev->ops.read = 0;
            p->dev->ops.write = 0;
            p->dev->priv = 0;
            p->dev = 0;
        }
        sata_port_dma_free(p);
        p->present = 0;
    }

    if (s->regs)
    {
        vmm_unmap_page((uint64_t)s->regs);
        pmm_free_frames((void *)(uintptr_t)s->regs_phys, s->regs_pages);
        s->regs = 0;
    }
    s->ready = 0;
    printk("SATA: module unloaded\n");
}
#endif /* !AXIOME_BUILTIN_DRIVER */

#ifndef AXIOME_BUILTIN_DRIVER
MODULE_INIT(sata_mod_init);
MODULE_EXIT(sata_mod_exit);
#endif
