#include "e1000.h"
#include "net_buf.h"
#include "netdev.h"
#include "net_util.h"
#include "pci.h"
#include "vmm.h"
#include "pmm.h"
#include "io.h"
#include "ioapic.h"
#include "softirq.h"
#include "slab.h"
#include "string.h"
#include "printk.h"
#include "spinlock.h"
#include "driver.h"
#include "module.h"

static struct e1000_softc g_e1000_sc;
static spinlock_t g_e1000_tx_lock = SPINLOCK_INIT;

/* ---- MMIO helpers ---- */
static uint32_t e1000_eeprom_read(struct e1000_softc *sc, uint16_t reg)
{
    e1000_write(sc, E1000_EERD, ((uint32_t)reg << 8) | 0x01);
    for (int i = 0; i < 1000; i++)
    {
        uint32_t val = e1000_read(sc, E1000_EERD);
        if (val & 0x10) return (val >> 16) & 0xFFFF;
    }
    return 0xFFFF;
}

/* ---- Reset & init ---- */
static void e1000_reset(struct e1000_softc *sc)
{
    /* Global reset. */
    e1000_write(sc, E1000_CTRL, E1000_CTRL_RST);
    for (volatile int i = 0; i < 100000; i++) {}

    /* Clear any pending interrupts. */
    e1000_read(sc, E1000_ICR);
}

static void e1000_init_rings(struct e1000_softc *sc)
{
    /* Allocate RX ring (page-aligned, physically contiguous). */
    void *rx_phys_page = pmm_alloc_frame();
    if (!rx_phys_page) { printk("E1000: failed to alloc RX ring page\n"); return; }
    sc->rx_ring = (struct e1000_rx_desc *)vmm_mmap_phys((uint64_t)rx_phys_page, 1, MMU_WRITE);
    memset(sc->rx_ring, 0, PAGE_SIZE);

    /* Allocate TX ring. */
    void *tx_phys_page = pmm_alloc_frame();
    if (!tx_phys_page) { printk("E1000: failed to alloc TX ring page\n"); return; }
    sc->tx_ring = (struct e1000_tx_desc *)vmm_mmap_phys((uint64_t)tx_phys_page, 1, MMU_WRITE);
    memset(sc->tx_ring, 0, PAGE_SIZE);

    /* Fill RX ring with mbufs. */
    sc->rx_head = 0;
    sc->rx_tail = E1000_RX_DESC_COUNT - 1;

    for (int i = 0; i < E1000_RX_DESC_COUNT; i++)
    {
        struct mbuf *m = mbuf_alloc();
        if (!m) { printk("E1000: mbuf alloc failed at desc %d\n", i); break; }
        sc->rx_mbufs[i] = m;
        sc->rx_ring[i].addr = m->phys;
        sc->rx_ring[i].status = 0;
        sc->rx_ring[i].length = 0;
        sc->rx_ring[i].checksum = 0;
        sc->rx_ring[i].errors = 0;
        sc->rx_ring[i].special = 0;
    }

    /* TX ring starts empty. */
    sc->tx_head = 0;
    sc->tx_tail = 0;
    memset(sc->tx_mbufs, 0, sizeof(sc->tx_mbufs));

    /* Program ring base addresses and lengths. */
    e1000_write(sc, E1000_RDBAL, (uint32_t)(uintptr_t)rx_phys_page);
    e1000_write(sc, E1000_RDBAH, 0);
    e1000_write(sc, E1000_RDLEN, E1000_RX_DESC_COUNT * sizeof(struct e1000_rx_desc));
    e1000_write(sc, E1000_RDH, 0);
    e1000_write(sc, E1000_RDT, E1000_RX_DESC_COUNT - 1);

    e1000_write(sc, E1000_TDBAL, (uint32_t)(uintptr_t)tx_phys_page);
    e1000_write(sc, E1000_TDBAH, 0);
    e1000_write(sc, E1000_TDLEN, E1000_TX_DESC_COUNT * sizeof(struct e1000_tx_desc));
    e1000_write(sc, E1000_TDH, 0);
    e1000_write(sc, E1000_TDT, 0);
}

static void e1000_read_mac(struct e1000_softc *sc, uint8_t *mac)
{
    /* Try reading from EEPROM first. */
    uint16_t w0 = e1000_eeprom_read(sc, 0);
    uint16_t w1 = e1000_eeprom_read(sc, 1);
    uint16_t w2 = e1000_eeprom_read(sc, 2);

    if (w0 != 0xFFFF || w1 != 0xFFFF || w2 != 0xFFFF)
    {
        mac[0] = w0 & 0xFF;
        mac[1] = (w0 >> 8) & 0xFF;
        mac[2] = w1 & 0xFF;
        mac[3] = (w1 >> 8) & 0xFF;
        mac[4] = w2 & 0xFF;
        mac[5] = (w2 >> 8) & 0xFF;
        return;
    }

    /* Fallback: read RAL0/RAH0 registers (some emulated NICs put MAC there). */
    uint32_t ral = e1000_read(sc, E1000_RAL0);
    uint32_t rah = e1000_read(sc, E1000_RAH0);
    mac[0] = ral & 0xFF;
    mac[1] = (ral >> 8) & 0xFF;
    mac[2] = (ral >> 16) & 0xFF;
    mac[3] = (ral >> 24) & 0xFF;
    mac[4] = rah & 0xFF;
    mac[5] = (rah >> 8) & 0xFF;

    /* If RAL/RAH are zero, use QEMU default MAC. */
    if (ral == 0 && rah == 0)
    {
        mac[0] = 0x52; mac[1] = 0x54; mac[2] = 0x00;
        mac[3] = 0x12; mac[4] = 0x34; mac[5] = 0x56;
    }
}

/* Program MAC into RAL0/RAH0 so the NIC's receive address filter accepts
   packets addressed to this MAC.  Must be called after e1000_read_mac. */
static void e1000_set_mac(struct e1000_softc *sc, const uint8_t *mac)
{
    uint32_t ral = mac[0] | (mac[1] << 8) | (mac[2] << 16) | (mac[3] << 24);
    uint32_t rah = mac[4] | (mac[5] << 8) | 0x80000000; /* AV (Address Valid) bit */
    e1000_write(sc, E1000_RAL0, ral);
    e1000_write(sc, E1000_RAH0, rah);
}

static void e1000_enable(struct e1000_softc *sc)
{
    /* Set link up and auto-speed detection — read-modify-write. */
    uint32_t ctrl = e1000_read(sc, E1000_CTRL);
    ctrl |= E1000_CTRL_SLU | E1000_CTRL_ASDE;
    e1000_write(sc, E1000_CTRL, ctrl);

    /* Enable RX: no checksum offload for simplicity, accept broadcast. */
    e1000_write(sc, E1000_RCTL, E1000_RCTL_EN | E1000_RCTL_BAM |
                                 E1000_RCTL_SZ_2048 | E1000_RCTL_SECRC);

    /* Enable TX. */
    e1000_write(sc, E1000_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP |
                                 E1000_TCTL_CT | E1000_TCTL_COLD);

    /* Enable all interrupts we care about. */
    e1000_write(sc, E1000_IMS, E1000_ICR_RXT0 | E1000_ICR_TXDW |
                                E1000_ICR_LSC | E1000_ICR_INTA);
}

/* ---- IRQ handler (runs in IRQ context — keep minimal) ---- */
static void e1000_irq_handler(void)
{
    uint32_t icr = e1000_read(&g_e1000_sc, E1000_ICR);

    /* RX and TX processing is done in the main poll loop.
       We just need to acknowledge the interrupt here. */

    if (icr & E1000_ICR_LSC)
    {
        /* Link status change — nothing to do in the minimal driver. */
    }
}

/* ---- RX processing (called from softirq or poll loop) ---- */
void e1000_rx_poll(void)
{
    struct e1000_softc *sc = &g_e1000_sc;

    while (1)
    {
        uint16_t idx = sc->rx_head;
        if (!(sc->rx_ring[idx].status & E1000_RXD_STAT_DD))
            break;

        struct mbuf *m = sc->rx_mbufs[idx];
        if (!m)
        {
            /* Replace with a fresh mbuf. */
            m = mbuf_alloc();
            if (!m) break;
            sc->rx_mbufs[idx] = m;
            sc->rx_ring[idx].addr = m->phys;
            sc->rx_ring[idx].status = 0;
            sc->rx_ring[idx].length = 0;
        }

        uint16_t len = sc->rx_ring[idx].length;
        m->len = len;
        m->data_off = 0;

        /* Advance software head for next poll. */
        sc->rx_head = (idx + 1) % E1000_RX_DESC_COUNT;

        /* Deliver to protocol stack. */
        struct mbuf *new_m = mbuf_alloc();
        if (new_m)
        {
            memcpy(new_m->data, m->data, len);
            new_m->len = len;
            sc->rx_mbufs[idx] = new_m;
            sc->rx_ring[idx].addr = new_m->phys;
            sc->rx_ring[idx].status = 0;
            sc->rx_ring[idx].length = 0;

            /* Tell hardware it can use descriptors up to (but not including)
               `idx`.  Setting RDT = idx means the hardware can write to
               descriptors [RDH, idx), which includes the re-armed `idx` once
               the hardware wraps past it. */
            e1000_write(sc, E1000_RDT, idx);
            netdev_rx_poll(&sc->netdev, new_m);
        }
        else
        {
            /* Out of mbufs — keep using the same one (reinsert). */
            m->len = 0;
            sc->rx_ring[idx].status = 0;
            sc->rx_ring[idx].length = 0;
        }
    }
}

/* ---- TX path (called from ethernet_send → netdev->tx) ---- */
static int e1000_tx(struct netdev *dev, struct mbuf *m)
{
    struct e1000_softc *sc = (struct e1000_softc *)dev->priv;
    if (!sc) { mbuf_free(m); return -1; }

    unsigned long flags = spin_lock_irq(&g_e1000_tx_lock);

    uint16_t next = (sc->tx_tail + 1) % E1000_TX_DESC_COUNT;
    if (next == sc->tx_head)
    {
        /* TX ring full. */
        spin_unlock_irq(&g_e1000_tx_lock, flags);
        mbuf_free(m);
        return -1;
    }

    /* Flatten the mbuf chain into a single buffer for DMA. */
    size_t total = mbuf_total_len(m);
    if (total > 2048) { spin_unlock_irq(&g_e1000_tx_lock, flags); mbuf_free(m); return -1; }

    struct mbuf *tx_m = mbuf_alloc();
    if (!tx_m) { spin_unlock_irq(&g_e1000_tx_lock, flags); mbuf_free(m); return -1; }

    /* Copy data from chain into single buffer. */
    size_t off = 0;
    struct mbuf *seg = m;
    while (seg && off < total)
    {
        size_t n = seg->len;
        if (n > total - off) n = total - off;
        memcpy(tx_m->data + off, seg->data + seg->data_off, n);
        off += n;
        seg = seg->next_seg;
    }
    tx_m->len = total;

    sc->tx_mbufs[sc->tx_tail] = tx_m;
    sc->tx_ring[sc->tx_tail].addr = tx_m->phys;
    sc->tx_ring[sc->tx_tail].length = (uint16_t)total;
    sc->tx_ring[sc->tx_tail].cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS;
    sc->tx_ring[sc->tx_tail].status = 0;

    sc->tx_tail = next;
    e1000_write(sc, E1000_TDT, sc->tx_tail);

    spin_unlock_irq(&g_e1000_tx_lock, flags);

    /* Free the original mbuf chain (we copied into tx_m). */
    mbuf_free(m);

    /* Check for completed TX descriptors and free them. */
    while (sc->tx_ring[sc->tx_head].status & E1000_TXD_STAT_DD)
    {
        if (sc->tx_mbufs[sc->tx_head])
        {
            mbuf_free(sc->tx_mbufs[sc->tx_head]);
            sc->tx_mbufs[sc->tx_head] = 0;
        }
        sc->tx_head = (sc->tx_head + 1) % E1000_TX_DESC_COUNT;
    }

    return 0;
}

/* ---- Probe (called by driver framework) ---- */
int e1000_probe(struct pci_device *pdev)
{
    struct e1000_softc *sc = &g_e1000_sc;
    memset(sc, 0, sizeof(*sc));

    sc->irq = pdev->irq;

    /* Map BAR0 (MMIO). */
    uint64_t bar0 = pci_bar_addr(pdev, 0);
    if (!bar0)
    {
        printk("E1000: BAR0 not available\n");
        return -1;
    }

    /* Determine BAR size by writing all 1s and reading back. */
    sc->mmio_phys = bar0;
    sc->mmio_size = 0x20000;  /* typical size for e1000 */

    sc->mmio = (volatile uint8_t *)vmm_mmap_phys(bar0,
                (sc->mmio_size + PAGE_SIZE - 1) / PAGE_SIZE, MMU_WRITE);
    if (!sc->mmio)
    {
        printk("E1000: failed to map BAR0 at 0x%lx\n", bar0);
        return -1;
    }

    printk("E1000: BAR0 mapped at phys 0x%lx -> virt %p\n", bar0, (void *)sc->mmio);

    /* Enable PCI bus mastering (required for DMA) and memory space access. */
    uint32_t cmd = pci_read32(pdev->bus, pdev->dev, pdev->func, 0x04);
    cmd |= 0x06; /* Bit 1: Memory Space, Bit 2: Bus Master */
    pci_write32(pdev->bus, pdev->dev, pdev->func, 0x04, cmd);

    /* Reset the device. */
    e1000_reset(sc);

    /* Read MAC address. */
    e1000_read_mac(sc, sc->netdev.mac);

    /* Write MAC into RAL0/RAH0 so the NIC accept filter works. */
    e1000_set_mac(sc, sc->netdev.mac);

    /* Set up the generic netdev. */
    strcpy(sc->netdev.name, "eth0");
    sc->netdev.mtu = 1500;
    sc->netdev.tx = e1000_tx;
    sc->netdev.priv = sc;

    /* Configure default IP (tap networking: host=10.0.2.1, guest=10.0.2.15). */
    sc->netdev.ip = ip4_make(10, 0, 2, 15);
    sc->netdev.netmask = ip4_make(255, 255, 255, 0);
    sc->netdev.gateway = ip4_make(10, 0, 2, 1);

    /* Initialize descriptor rings. */
    e1000_init_rings(sc);

    /* Enable device. */
    e1000_enable(sc);

    /* Register with the network stack. */
    netdev_register(&sc->netdev);

    printk("E1000: initialised (IRQ %d, IP %d.%d.%d.%d)\n",
           sc->irq,
           ip4_octet(sc->netdev.ip, 0), ip4_octet(sc->netdev.ip, 1),
           ip4_octet(sc->netdev.ip, 2), ip4_octet(sc->netdev.ip, 3));

    /* Install IRQ handler (legacy INTx via I/O APIC). */
    ioapic_mask(sc->irq, 0);  /* unmask */

    return 0;
}

/* ===========================================================================
 * Module packaging (.kxt)
 *
 * Built as a freestanding, -mcmodel=large relocatable ELF.  The driver
 * registers itself with the kernel driver framework and probes any PCI
 * device already enumerated at boot.
 * =========================================================================== */
static struct driver e1000_drv = {
    .name       = "e1000",
    .vendor     = 0x8086,
    .device     = DRV_ANY,
    .pci_class  = 0x02,
    .pci_subclass = 0x00,
    .probe      = e1000_probe,
};

static int e1000_mod_init(void)
{
    /* Let the kernel main loop poll our RX ring. */
    netdev_register_poll(e1000_rx_poll);

    /* Register with the driver framework, then probe any matching PCI
       device that was enumerated before this module loaded. */
    driver_register(&e1000_drv);
    for (struct pci_device *p = pci_first(); p; p = p->next)
        driver_probe_pci(p);

    printk("E1000: module initialised\n");
    return 0;
}

static void e1000_teardown(struct e1000_softc *sc)
{
    if (!sc->mmio)
        return;

    /* Stop the hardware from DMA'ing into our buffers. */
    e1000_write(sc, E1000_RCTL, e1000_read(sc, E1000_RCTL) & ~E1000_RCTL_EN);
    e1000_write(sc, E1000_TCTL, e1000_read(sc, E1000_TCTL) & ~E1000_TCTL_EN);

    /* Read back the ring base addresses before they are cleared. */
    uint64_t rx_phys = e1000_read(sc, E1000_RDBAL);
    uint64_t tx_phys = e1000_read(sc, E1000_TDBAL);

    /* Free RX/TX mbufs back to the shared pool. */
    for (int i = 0; i < E1000_RX_DESC_COUNT; i++)
        if (sc->rx_mbufs[i]) { mbuf_free(sc->rx_mbufs[i]); sc->rx_mbufs[i] = 0; }
    for (int i = 0; i < E1000_TX_DESC_COUNT; i++)
        if (sc->tx_mbufs[i]) { mbuf_free(sc->tx_mbufs[i]); sc->tx_mbufs[i] = 0; }

    /* Unmap and free the descriptor ring pages. */
    if (sc->rx_ring) { vmm_unmap_page((uint64_t)sc->rx_ring); pmm_free_frame((void *)(uintptr_t)rx_phys); sc->rx_ring = 0; }
    if (sc->tx_ring) { vmm_unmap_page((uint64_t)sc->tx_ring); pmm_free_frame((void *)(uintptr_t)tx_phys); sc->tx_ring = 0; }

    /* Unmap and free the MMIO BAR. */
    uint64_t nframes = (sc->mmio_size + PAGE_SIZE - 1) / PAGE_SIZE;
    vmm_unmap_page((uint64_t)sc->mmio);
    pmm_free_frames((void *)(uintptr_t)sc->mmio_phys, nframes);
    sc->mmio = 0;
}

static void e1000_mod_exit(void)
{
    /* Stop the kernel main loop from polling our RX ring before the code
       and data pages are freed, otherwise netdev_poll_all() dereferences a
       dangling function pointer. */
    netdev_unregister_poll(e1000_rx_poll);
    driver_unregister(&e1000_drv);
    e1000_teardown(&g_e1000_sc);
    printk("E1000: module unloaded\n");
}

MODULE_INIT(e1000_mod_init);
MODULE_EXIT(e1000_mod_exit);
