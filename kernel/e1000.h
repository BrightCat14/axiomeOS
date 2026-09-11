#ifndef AXIOME_E1000_H
#define AXIOME_E1000_H

#include <stdint.h>
#include "pci.h"
#include "netdev.h"

/* Intel e1000 (8254x) driver.
 * Registers are MMIO-mapped via BAR0.
 */

/* ---- Register offsets (BAR0) ---- */
#define E1000_CTRL     0x0000  /* Device Control */
#define E1000_STATUS   0x0008  /* Device Status */
#define E1000_EECD     0x0010  /* EEPROM/Flash Control */
#define E1000_EERD     0x0014  /* EEPROM Read */
#define E1000_CTRL_EXT 0x0018  /* Extended Device Control */
#define E1000_ICR      0x00C0  /* Interrupt Cause Read */
#define E1000_ICS      0x00C8  /* Interrupt Cause Set */
#define E1000_IMS      0x00D0  /* Interrupt Mask Set/Read */
#define E1000_RCTL     0x0100  /* Receive Control */
#define E1000_FCTRL    0x0508  /* Filter Control */
#define E1000_RAL0     0x5400  /* Receive Address Low (first MAC) */
#define E1000_RAH0     0x5404  /* Receive Address High (first MAC) */
#define E1000_TCTL     0x0400  /* Transmit Control */

/* RX descriptors */
#define E1000_RDBAL    0x02800 /* RX Descriptor Base Low */
#define E1000_RDBAH    0x02804 /* RX Descriptor Base High */
#define E1000_RDLEN    0x02808 /* RX Descriptor Ring Length */
#define E1000_RDH      0x02810 /* RX Descriptor Head */
#define E1000_RDT      0x02818 /* RX Descriptor Tail */

/* TX descriptors */
#define E1000_TDBAL    0x03800 /* TX Descriptor Base Low */
#define E1000_TDBAH    0x03804 /* TX Descriptor Base High */
#define E1000_TDLEN    0x03808 /* TX Descriptor Ring Length */
#define E1000_TDH      0x03810 /* TX Descriptor Head */
#define E1000_TDT      0x03818 /* TX Descriptor Tail */

/* RCTL bits */
#define E1000_RCTL_EN     (1 << 1)
#define E1000_RCTL_SBP    (1 << 2)
#define E1000_RCTL_UPE    (1 << 3)
#define E1000_RCTL_MPE    (1 << 4)
#define E1000_RCTL_BAM    (1 << 15)
#define E1000_RCTL_SZ_2048 (0 << 16)
#define E1000_RCTL_BSIZE  (3 << 16)
#define E1000_RCTL_SECRC  (1 << 26)

/* TCTL bits */
#define E1000_TCTL_EN     (1 << 1)
#define E1000_TCTL_PSP    (1 << 3)
#define E1000_TCTL_CT     (0x0F << 4)
#define E1000_TCTL_COLD   (0x40 << 12)

/* STATUS bits */
#define E1000_STATUS_LU   (1 << 1)

/* ICR bits */
#define E1000_ICR_TXDW    (1 << 0)
#define E1000_ICR_TXQE    (1 << 1)
#define E1000_ICR_LSC     (1 << 2)
#define E1000_ICR_RXDMT0  (1 << 4)
#define E1000_ICR_RXT0    (1 << 7)
#define E1000_ICR_INTA    (1 << 31)

/* CTRL bits */
#define E1000_CTRL_RST    (1 << 26)
#define E1000_CTRL_ASDE   (1 << 5)  /* Auto Speed Detection Enable */
#define E1000_CTRL_SLU    (1 << 6)  /* Set Link Up */

/* RX descriptor status bits */
#define E1000_RXD_STAT_DD  (1 << 0)  /* Descriptor Done */
#define E1000_RXD_STAT_EOP (1 << 1)  /* End of Packet */
#define E1000_RXD_STAT_IXSM (1 << 2) /* Ignore Checksum Indication */

/* TX descriptor CMD bits */
#define E1000_TXD_CMD_EOP  (1 << 0)  /* End of Packet */
#define E1000_TXD_CMD_IFCS (1 << 1)  /* Insert FCS */
#define E1000_TXD_CMD_RS   (1 << 3)  /* Report Status */

/* TX descriptor status bits */
#define E1000_TXD_STAT_DD  (1 << 0)

#define E1000_RX_DESC_COUNT 128
#define E1000_TX_DESC_COUNT 128
#define E1000_RX_BUFFER_SIZE 2048

/* RX descriptor (legacy format). */
struct e1000_rx_desc {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} __attribute__((packed));

/* TX descriptor (legacy format). */
struct e1000_tx_desc {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} __attribute__((packed));

/* Driver soft state. */
struct e1000_softc {
    volatile uint8_t *mmio;     /* MMIO base (virtual) */
    uint64_t mmio_phys;         /* MMIO base (physical) */
    uint32_t mmio_size;

    /* RX ring */
    struct e1000_rx_desc *rx_ring;
    struct mbuf *rx_mbufs[E1000_RX_DESC_COUNT];
    uint16_t rx_head;
    uint16_t rx_tail;

    /* TX ring */
    struct e1000_tx_desc *tx_ring;
    struct mbuf *tx_mbufs[E1000_TX_DESC_COUNT];
    uint16_t tx_head;
    uint16_t tx_tail;

    /* IRQ */
    uint8_t irq;

    /* PCI coords of the bound function + claimed flag. This driver owns a
       single static softc, so a second probe of the same NIC is a silent
       no-op and a different NIC is refused (returns -1). */
    uint8_t pci_bus, pci_dev, pci_func;
    int bound;

    struct netdev netdev;  /* generic netdev this driver feeds */
};

/* Probe function — called by driver framework. */
int e1000_probe(struct pci_device *pdev);

/* Poll for received packets (called from main loop). */
void e1000_rx_poll(void);

/* Read a 32-bit MMIO register. */
static inline uint32_t e1000_read(struct e1000_softc *sc, uint32_t off)
{
    return *(volatile uint32_t *)(sc->mmio + off);
}

/* Write a 32-bit MMIO register. */
static inline void e1000_write(struct e1000_softc *sc, uint32_t off, uint32_t val)
{
    *(volatile uint32_t *)(sc->mmio + off) = val;
}

#endif
