#ifndef AXIOME_XHCI_H
#define AXIOME_XHCI_H

#include <stdint.h>
#include "usb.h"

/* ------------------------------------------------------------------ *
 * Capability Registers (at BAR base + 0)
 * ------------------------------------------------------------------ */
#define XHCI_CAPLENGTH   0x00
#define XHCI_HCIVERSION  0x02
#define XHCI_HCSPARAMS1  0x04
#define XHCI_HCSPARAMS2  0x08
#define XHCI_HCSPARAMS3  0x0C
#define XHCI_HCCPARAMS1  0x10
#define XHCI_DBOFF       0x14
#define XHCI_RTSOFF      0x18
#define XHCI_HCCPARAMS2  0x1C

#define HCSPARAMS1_MAX_SLOTS(x)   ((x) & 0xFF)
#define HCSPARAMS1_MAX_INTRS(x)   (((x) >> 8) & 0x7FF)
#define HCSPARAMS1_MAX_PORTS(x)   (((x) >> 24) & 0xFF)
#define HCSPARAMS2_IST(x)         ((x) & 0xF)
#define HCSPARAMS2_ERST_MAX(x)    (((x) >> 4) & 0xF)
#define HCCPARAMS1_AC64(x)        ((x) & 1)
#define HCCPARAMS1_CSZ(x)         (((x) >> 2) & 1)
#define HCCPARAMS1_EC(x)          (((x) >> 30) & 1)

/* ------------------------------------------------------------------ *
 * Operational Registers (at BAR + CAPLENGTH)
 * ------------------------------------------------------------------ */
#define XHCI_USBCMD      0x00
#define XHCI_USBSTS      0x04
#define XHCI_PAGESIZE    0x08
#define XHCI_DNCTRL      0x14
#define XHCI_CRCR        0x18
#define XHCI_DCBAAP      0x30
#define XHCI_CONFIG      0x38

#define USBCMD_RUN       (1UL << 0)
#define USBCMD_HCRST     (1UL << 1)
#define USBCMD_INTE      (1UL << 2)
#define USBCMD_HSEE      (1UL << 3)
#define USBCMD_EWE       (1UL << 10)

#define USBSTS_HCH       (1UL << 0)
#define USBSTS_HSE       (1UL << 1)
#define USBSTS_EINT      (1UL << 2)
#define USBSTS_PCD       (1UL << 3)
#define USBSTS_CNR       (1UL << 11)
#define USBSTS_SRE       (1UL << 12)

#define CRCR_RCS         (1UL << 0)
#define CRCR_CS          (1UL << 1)
#define CRCR_CA          (1UL << 2)
#define CRCR_CRR         (1UL << 3)

#define CONFIG_MAX_SLOTS_EN(x)  ((uint32_t)(x))

/* ------------------------------------------------------------------ *
 * Port Registers (at BAR + 0x400)
 * ------------------------------------------------------------------ */
#define XHCI_PORT_REGS_SIZE  16

#define PORTSC_CCS       (1UL << 0)
#define PORTSC_PED       (1UL << 1)
#define PORTSC_PR        (1UL << 4)
#define PORTSC_PP        (1UL << 9)
#define PORTSC_CSC       (1UL << 17)
#define PORTSC_PLC       (1UL << 18)
#define PORTSC_PRC       (1UL << 19)
#define PORTSC_WRC       (1UL << 20)
#define PORTSC_OCC       (1UL << 21)
#define PORTSC_CHANGE_BITS (PORTSC_CSC | PORTSC_PLC | PORTSC_PRC | PORTSC_WRC | PORTSC_OCC)
#define PORTSC_PLS(x)    (((x) >> 5) & 0xF)
#define PORTSC_SPEED(x)  (((x) >> 10) & 0xF)

#define PLS_U0       0
#define PLS_U3       3
#define PLS_DISABLED 4
#define PLS_RX_DET   5

/* ------------------------------------------------------------------ *
 * Runtime Registers (at BAR + RTSOFF)
 * ------------------------------------------------------------------ */
#define XHCI_MFINDEX  0x00
#define XHCI_IR_OFF(n)  (0x20 + (n) * 32)

#define IR_IMAN        0x00
#define IR_IMOD        0x04
#define IR_ERSTSZ      0x08
#define IR_ERSTBA      0x10
#define IR_ERDP        0x18

#define IMAN_IP        (1UL << 0)
#define IMAN_IE        (1UL << 1)

/* ------------------------------------------------------------------ *
 * Doorbell Registers
 * ------------------------------------------------------------------ */
#define XHCI_DB_OFF(n)  ((n) * 4)
#define DB_TARGET(x)    ((uint32_t)(x) & 0xFF)

/* ------------------------------------------------------------------ *
 * TRB Constants
 * ------------------------------------------------------------------ */
#define TRB_SIZE        16

#define TRB_TYPE_NORMAL         1
#define TRB_TYPE_SETUP_STAGE    2
#define TRB_TYPE_DATA_STAGE     3
#define TRB_TYPE_STATUS_STAGE   4
#define TRB_TYPE_LINK           6
#define TRB_TYPE_EVENT_DATA     7
#define TRB_TYPE_ENABLE_SLOT    9
#define TRB_TYPE_DISABLE_SLOT   10
#define TRB_TYPE_ADDRESS_DEV    11
#define TRB_TYPE_CONFIGURE_EP   12
#define TRB_TYPE_EVAL_CONTEXT   13
#define TRB_TYPE_RESET_EP       14
#define TRB_TYPE_STOP_EP        15
#define TRB_TYPE_SET_TR_DEQUEUE 16
#define TRB_TYPE_RESET_DEV      17
#define TRB_TYPE_NOOP_CMD       24

#define EVT_TRANSFER            1
#define EVT_COMMAND_COMPLETE    2
#define EVT_PORT_STATUS_CHANGE  3

#define TRB_CHAIN       (1UL << 4)
#define TRB_IOC         (1UL << 5)
#define TRB_IDT         (1UL << 6)
#define TRB_ISP         (1UL << 2)
#define TRB_ED          (1UL << 4)
#define TRB_TR_DIR      (1UL << 16)

#define CC_SUCCESS      1
#define CC_ERROR        2

/* ------------------------------------------------------------------ *
 * Context definitions
 * ------------------------------------------------------------------ */
#define XHCI_CTX_SIZE   32
#define XHCI_MAX_EPS    31
#define XHCI_DEV_CTX_SIZE  (XHCI_CTX_SIZE * (1 + XHCI_MAX_EPS))

#define SLOT_CTX_SPEED(x)            (((uint32_t)(x) & 0xF) << 20)
#define SLOT_CTX_CONTEXT_ENTRIES(x)  (((uint32_t)(x) & 0x1F) << 27)
#define SLOT_CTX_ROOT_HUB_PORT(x)    (((uint32_t)(x) & 0xFF) << 16)
#define SLOT_CTX_INTERRUPTER(x)      (((uint32_t)(x) & 0xFF) << 22)

#define EP_CTX_EP_TYPE(x)            (((uint32_t)(x) & 0x7) << 3)
#define EP_CTX_CERR(x)               (((uint32_t)(x) & 0x3) << 1)
#define EP_CTX_MAX_PACKET_SIZE(x)    ((uint32_t)(x) << 16)
#define EP_CTX_AVG_TRB_LENGTH(x)     ((uint32_t)(x) << 16)
#define EP_CTX_MAX_BURST_SIZE(x)     (((uint32_t)(x) & 0xFF) << 8)

/* xHCI endpoint type values for EP context (different from USB spec values) */
#define XHCI_EP_TYPE_CONTROL     4
#define XHCI_EP_TYPE_BULK_OUT    2
#define XHCI_EP_TYPE_BULK_IN     6
#define XHCI_EP_TYPE_INTERRUPT_OUT 3
#define XHCI_EP_TYPE_INTERRUPT_IN  7

#define SPEED_FULL  1
#define SPEED_LOW   2
#define SPEED_HIGH  3
#define SPEED_SUPER 4

/* ------------------------------------------------------------------ *
 * TRB data structure (16 bytes)
 * ------------------------------------------------------------------ */
typedef struct __attribute__((packed)) {
    uint64_t ptr;
    uint32_t status;
    uint32_t flags;
} xhci_trb_t;

#define TRB_DW2_TYPE(x)    (((uint32_t)(x) & 0x3F) << 10)
#define TRB_DW2_SLOT(x)    (((uint32_t)(x) & 0xFF) << 24)
#define EVT_DW3_TYPE(x)    (((x) >> 10) & 0x3F)
#define EVT_DW3_SLOT(x)    (((x) >> 24) & 0xFF)
#define CCE_DW0_SLOT(x)    ((uint32_t)(x) & 0x3F)
#define TRB_STATUS_CC(x)   (((uint32_t)(x) >> 24) & 0xFF)

/* Input Control Context */
typedef struct __attribute__((packed)) {
    uint32_t drop_flags;
    uint32_t add_flags;
} xhci_input_ctrl_t;

/* ------------------------------------------------------------------ *
 * Ring
 * ------------------------------------------------------------------ */
enum { TRBS_PER_SEG = 256, TRB_LINK_SLOT = TRBS_PER_SEG - 1 };
#define XHCI_ERST_SIZE    1

struct xhci_ring {
    xhci_trb_t *trbs;
    uintptr_t   phys;
    int         cycle;
    int         enq_idx;
};

/* Event Ring Segment Table Entry */
typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint32_t size;
    uint32_t rsvd;
} xhci_erst_entry_t;

/* ------------------------------------------------------------------ *
 * xHCI Device (per connected device)
 * ------------------------------------------------------------------ */
struct xhci_device {
    int slot_id;
    int port;
    int speed;
    uint8_t address;
    int configured;

    struct xhci_ring ep_rings[32];
    uintptr_t dev_ctx_phys;
    void     *dev_ctx_virt;
    uintptr_t input_ctx_phys;
    void     *input_ctx_virt;

    struct usb_device usb_dev;
};

/* ------------------------------------------------------------------ *
 * xHCI Controller State
 * ------------------------------------------------------------------ */
struct xhci_controller {
    volatile uint8_t  *capl;
    volatile uint32_t *op;
    volatile uint32_t *port;
    volatile uint32_t *rt;
    volatile uint32_t *db;

    uintptr_t phys_base;
    int max_slots;
    int max_ports;
    int max_intrs;
    int context_size;
    int ac64;

    struct xhci_ring cmd_ring;
    uintptr_t dcbaa_phys;
    uint64_t *dcbaa;

    xhci_trb_t  *evt_ring;
    uintptr_t    evt_ring_phys;
    xhci_erst_entry_t *erst;
    uintptr_t    erst_phys;
    int evt_deq_idx;
    int evt_cycle;

    struct pci_device *pci_dev;
    int irq_vector;
    int qemu_workaround;

    struct xhci_device devices[256];
    int num_devices;

    struct hcd_ops ops;
};

/* ------------------------------------------------------------------ *
 * Functions
 * ------------------------------------------------------------------ */
int  xhci_probe(struct pci_device *pdev);
void xhci_poll_events(void);

#endif
