#include "xhci.h"
#include "acpi.h"
#include "pci.h"
#include "vmm.h"
#include "pmm.h"
#include "slab.h"
#include "printk.h"
#include "string.h"
#include "driver.h"
#include "irq.h"
#include "apic.h"
#include "usb_msd.h"

/* ------------------------------------------------------------------ *
 * MMIO helpers
 * ------------------------------------------------------------------ */
static inline uint8_t  xhci_read8(volatile uint8_t *base, uint32_t off)
{ return base[off]; }

static inline uint32_t xhci_read32(volatile uint32_t *base, uint32_t off)
{ return base[off / 4]; }

static inline void xhci_write32(volatile uint32_t *base, uint32_t off, uint32_t v)
{ base[off / 4] = v; }

static inline uint64_t xhci_read64(volatile uint32_t *base, uint32_t off)
{
    uint32_t lo = base[off / 4];
    uint32_t hi = base[off / 4 + 1];
    return ((uint64_t)hi << 32) | lo;
}

static inline void xhci_write64(volatile uint32_t *base, uint32_t off, uint64_t v)
{
    base[off / 4]     = (uint32_t)(v & 0xFFFFFFFF);
    base[off / 4 + 1] = (uint32_t)(v >> 32);
}

static inline uint64_t xhci_op_read64(struct xhci_controller *xc, uint32_t off)
{
    if (xc->ac64)
        return xhci_read64(xc->op, off);
    return xhci_read32(xc->op, off);
}

static inline void xhci_op_write64(struct xhci_controller *xc, uint32_t off, uint64_t v)
{
    if (xc->ac64)
        xhci_write64(xc->op, off, v);
    else
        xhci_write32(xc->op, off, (uint32_t)(v & 0xFFFFFFFF));
}

/* ------------------------------------------------------------------ *
 * Physical memory allocation
 * ------------------------------------------------------------------ */
static void *alloc_phys_pages(size_t count, uintptr_t *phys_out)
{
    size_t n = count ? count : 1;
    uintptr_t p = (uintptr_t)pmm_alloc_frames(n);
    if (!p) return 0;
    void *v = vmm_mmap_phys(p, n, PTE_WRITE);
    if (!v) { pmm_free_frames((void *)p, n); return 0; }
    memset(v, 0, n * PAGE_SIZE);
    if (phys_out) *phys_out = p;
    return v;
}

static void free_phys_pages(void *virt, uintptr_t phys, size_t count)
{
    size_t n = count ? count : 1;
    vmm_unmap_page((uintptr_t)virt);
    pmm_free_frames((void *)phys, n);
}

/* ------------------------------------------------------------------ *
 * Ring operations
 * ------------------------------------------------------------------ */
static int ring_init(struct xhci_ring *r)
{
    size_t sz = TRBS_PER_SEG * TRB_SIZE;
    size_t pages = (sz + PAGE_SIZE - 1) / PAGE_SIZE;
    uintptr_t phys;
    r->trbs = (xhci_trb_t *)alloc_phys_pages(pages, &phys);
    if (!r->trbs) return -1;
    memset(r->trbs, 0, sz);
    r->phys = phys;
    r->cycle = 1;
    r->enq_idx = 0;
    return 0;
}

static void ring_free(struct xhci_ring *r)
{
    if (!r->trbs) return;
    size_t sz = TRBS_PER_SEG * TRB_SIZE;
    size_t pages = (sz + PAGE_SIZE - 1) / PAGE_SIZE;
    free_phys_pages(r->trbs, r->phys, pages);
    r->trbs = 0;
}

static void ring_enqueue_one(struct xhci_ring *r, xhci_trb_t *src)
{
    xhci_trb_t *slot = &r->trbs[r->enq_idx];
    memcpy(slot, src, TRB_SIZE);
    slot->flags |= (r->cycle ? 1 : 0);

    r->enq_idx++;
    if (r->enq_idx == TRB_LINK_SLOT)
    {
        xhci_trb_t link;
        memset(&link, 0, sizeof(link));
        link.ptr = r->phys;
        link.flags = TRB_TYPE_LINK | (6UL << 10) | 2 | ((r->cycle ^ 1) & 1);
        memcpy(&r->trbs[TRB_LINK_SLOT], &link, TRB_SIZE);
        r->enq_idx = 0;
        r->cycle ^= 1;
    }
}

/* ------------------------------------------------------------------ *
 * Doorbell
 * ------------------------------------------------------------------ */
static inline void xhci_ring_doorbell(struct xhci_controller *xc, int slot, int target)
{
    xhci_write32(xc->db, XHCI_DB_OFF(slot), DB_TARGET(target));
}

/* ------------------------------------------------------------------ *
 * Port access
 * ------------------------------------------------------------------ */
static uint32_t xhci_port_read(struct xhci_controller *xc, int port, uint32_t reg_off)
{
    volatile uint32_t *base = (volatile uint32_t *)((uintptr_t)xc->port + port * XHCI_PORT_REGS_SIZE);
    return base[reg_off / 4];
}

static void xhci_port_write(struct xhci_controller *xc, int port, uint32_t reg_off, uint32_t v)
{
    volatile uint32_t *base = (volatile uint32_t *)((uintptr_t)xc->port + port * XHCI_PORT_REGS_SIZE);
    base[reg_off / 4] = v;
}

/* ------------------------------------------------------------------ *
 * Command / Event shared state
 * ------------------------------------------------------------------ */
static struct xhci_controller *g_xhc;

static volatile struct cmd_completion {
    volatile int done;
    volatile uint32_t status;
    volatile uint32_t slot_id;
} g_cmd_comp;
static volatile int g_cmd_pending;

struct xhci_transfer_completion {
    volatile int done;
    volatile uint32_t status;
};

static volatile struct xhci_transfer_completion g_tr_comp;

static int xhci_control_transfer(struct usb_device *udev,
                                 struct usb_setup_packet *setup,
                                 void *data, size_t data_len);

static int xhci_configure_endpoint(struct xhci_controller *xc,
                                    struct xhci_device *dev,
                                    int ep_addr, int ep_type,
                                    int max_packet_size);

static int xhci_ep_dci(int ep_addr)
{
    int ep_num = ep_addr & 0x0F;
    if (ep_num == 0) return 1;
    int dir_in = (ep_addr & 0x80) ? 1 : 0;
    return ep_num * 2 + dir_in;
}

static struct xhci_ring *xhci_get_ep_ring(struct xhci_device *dev, int ep_addr)
{
    int dci = xhci_ep_dci(ep_addr);
    if (dci < 1 || dci >= 32) return 0;
    return &dev->ep_rings[dci];
}

static void cmd_completion_handler(uint32_t status, uint32_t slot_id)
{
    if (g_cmd_pending)
    {
        g_cmd_comp.status = status;
        g_cmd_comp.slot_id = slot_id;
        g_cmd_comp.done = 1;
    }
}

/* Event ring: drain available events without requiring IMAN_IP.
   Returns number of events processed. */
static int xhci_drain_events(struct xhci_controller *xc)
{
    volatile uint32_t *ir_base = &xc->rt[XHCI_IR_OFF(0) / 4];
    int processed = 0;

    while (processed < TRBS_PER_SEG)
    {
        xhci_trb_t *evt = &xc->evt_ring[xc->evt_deq_idx];
        int evt_cycle = evt->flags & 1;

        if (evt_cycle != xc->evt_cycle)
            break;

        uint32_t dw0 = (uint32_t)(evt->ptr & 0xFFFFFFFF);
        uint32_t dw2 = evt->status;
        uint32_t dw3 = evt->flags;
        uint32_t slot_id = (dw3 >> 24) & 0xFF;
        uint32_t cc = TRB_STATUS_CC(dw2);
        int type = (dw3 >> 10) & 0x3F;

        printk("XHCI: evt deq=%d raw=[0x%x,0x%x,0x%x,0x%x] cc=%d type=%d slot=%u\n",
               processed, dw0, (uint32_t)(evt->ptr >> 32), dw2, dw3,
               cc, type, slot_id);

        if (type == 33)
            cmd_completion_handler(dw2, slot_id);
        else if (type == 32)
            g_tr_comp.done = 1;

        evt->status = 0;
        evt->flags = 0;
        processed++;
        xc->evt_deq_idx++;
        if (xc->evt_deq_idx == TRBS_PER_SEG)
        {
            xc->evt_deq_idx = 0;
            xc->evt_cycle ^= 1;
        }
    }

    if (processed)
    {
        uint64_t erdp = xc->evt_ring_phys + (uint64_t)xc->evt_deq_idx * TRB_SIZE;
        xhci_write64(ir_base, IR_ERDP, erdp | (1UL << 3));
        xhci_write32(ir_base, IR_IMAN, IMAN_IP | IMAN_IE);
    }

    return processed;
}

static void xhci_process_events(struct xhci_controller *xc)
{
    volatile uint32_t *ir_base = &xc->rt[XHCI_IR_OFF(0) / 4];
    if (!(xhci_read32(ir_base, IR_IMAN) & IMAN_IP))
        return;
    xhci_drain_events(xc);
}

static int xhci_send_cmd(struct xhci_controller *xc, xhci_trb_t *trb)
{
    g_cmd_comp.done = 0;
    g_cmd_comp.status = 0;
    g_cmd_comp.slot_id = 0;
    g_cmd_pending = 1;

    ring_enqueue_one(&xc->cmd_ring, trb);
    xhci_ring_doorbell(xc, 0, 0);

    int timeout = 3000000;
    while (!g_cmd_comp.done && timeout--)
    {
        xhci_drain_events(xc);
        __asm__ volatile("pause");
    }
    g_cmd_pending = 0;

    if (!g_cmd_comp.done)
    {
        printk("XHCI: cmd timeout!\n");
        return -1;
    }

    return TRB_STATUS_CC(g_cmd_comp.status);
}

/* ------------------------------------------------------------------ *
 * IRQ handler
 * ------------------------------------------------------------------ */
static void xhci_irq_handler(struct isr_frame *frame, void *priv)
{
    (void)frame;
    struct xhci_controller *xc = (struct xhci_controller *)priv;
    xhci_process_events(xc);
    apic_eoi();
}

/* ------------------------------------------------------------------ *
 * MSI setup
 * ------------------------------------------------------------------ */
static int xhci_setup_msi(struct xhci_controller *xc)
{
    struct pci_device *pdev = xc->pci_dev;

    uint8_t msi_cap = pci_find_cap(pdev->bus, pdev->dev, pdev->func, 0x05);
    uint8_t msix_cap = pci_find_cap(pdev->bus, pdev->dev, pdev->func, 0x11);

    uint8_t vector = 0x40;

    if (msix_cap)
    {
        uint32_t bir = pci_read32(pdev->bus, pdev->dev, pdev->func, msix_cap + 4);
        int bir_idx = bir & 7;
        int table_offset = bir & ~7;

        uint64_t bar_phys = pci_bar_addr(pdev, bir_idx);
        if (!bar_phys)
            return -1;

        uint64_t table_phys = bar_phys + table_offset;
        size_t table_pages = 1;
        void *table_virt = vmm_mmap_phys(table_phys, table_pages, PTE_WRITE);
        if (!table_virt)
            return -1;

        volatile uint32_t *entry = (volatile uint32_t *)table_virt;
        entry[0] = 0xFEE00000u;
        entry[1] = 0;
        entry[2] = vector;
        entry[3] = 1;

        uint16_t flags = (uint16_t)pci_read32(pdev->bus, pdev->dev, pdev->func, msix_cap);
        flags |= (1 << 15);
        pci_write32(pdev->bus, pdev->dev, pdev->func, msix_cap, flags);

        printk("XHCI: MSI-X enabled at vector 0x%x\n", vector);
    }
    else if (msi_cap)
    {
        if (pci_msi_enable(pdev->bus, pdev->dev, pdev->func, vector) != 0)
        {
            printk("XHCI: MSI enable failed\n");
            return -1;
        }
        printk("XHCI: MSI enabled at vector 0x%x\n", vector);
    }
    else
    {
        printk("XHCI: no MSI/MSI-X, using PIN IRQ\n");
        return -1;
    }

    if (irq_register(vector, xhci_irq_handler, xc) != 0)
    {
        printk("XHCI: irq_register failed\n");
        return -1;
    }

    xc->irq_vector = vector;

    extern void idt_set_gate(uint8_t vector, uintptr_t handler, uint8_t flags, uint8_t ist);
    extern void isr_msi0x40(void);
    idt_set_gate(vector, (uintptr_t)isr_msi0x40, 0x8E, 0);

    return 0;
}

/* ------------------------------------------------------------------ *
 * Controller initialization
 * ------------------------------------------------------------------ */
static void xhci_print_caps(struct xhci_controller *xc)
{
    uint16_t ver = *(volatile uint16_t *)&xc->capl[XHCI_HCIVERSION];
    uint32_t hsp1 = xhci_read32((volatile uint32_t *)xc->capl, XHCI_HCSPARAMS1);
    uint32_t hcc1 = xhci_read32((volatile uint32_t *)xc->capl, XHCI_HCCPARAMS1);
    printk("XHCI: CAPLENGTH=%u HCIVERSION=0x%x MaxSlots=%u MaxPorts=%u AC64=%u CSZ=%u\n",
           xhci_read8(xc->capl, XHCI_CAPLENGTH), ver,
           xc->max_slots, xc->max_ports, xc->ac64, xc->context_size == 128 ? 1 : 0);
    printk("XHCI: HCSPARAMS1=0x%x HCCPARAMS1=0x%x\n", hsp1, hcc1);
}

static int xhci_reset_hc(struct xhci_controller *xc)
{
    xhci_write32(xc->op, XHCI_USBCMD, USBCMD_HCRST);
    int timeout = 100000;
    while (xhci_read32(xc->op, XHCI_USBCMD) & USBCMD_HCRST)
    {
        if (!timeout--) return -1;
        __asm__ volatile("pause");
    }
    printk("XHCI: HC reset complete\n");
    return 0;
}

static int xhci_wait_halt(struct xhci_controller *xc, int halted)
{
    int timeout = 100000;
    while (!!(xhci_read32(xc->op, XHCI_USBSTS) & USBSTS_HCH) != !!halted)
    {
        if (!timeout--) return -1;
        __asm__ volatile("pause");
    }
    return 0;
}

static int xhci_init(struct xhci_controller *xc)
{
    uint32_t hsp1 = xhci_read32((volatile uint32_t *)xc->capl, XHCI_HCSPARAMS1);
    uint32_t hcc1 = xhci_read32((volatile uint32_t *)xc->capl, XHCI_HCCPARAMS1);

    xc->max_slots = HCSPARAMS1_MAX_SLOTS(hsp1);
    xc->max_ports = HCSPARAMS1_MAX_PORTS(hsp1);
    xc->max_intrs = HCSPARAMS1_MAX_INTRS(hsp1);
    xc->ac64 = HCCPARAMS1_AC64(hcc1);
    xc->context_size = HCCPARAMS1_CSZ(hcc1) ? 64 : 32;

    if (xc->max_slots == 0 || xc->max_ports == 0)
    {
        printk("XHCI: invalid params slots=%d ports=%d\n",
               xc->max_slots, xc->max_ports);
        return -1;
    }

    xhci_print_caps(xc);

    uint32_t sts = xhci_read32(xc->op, XHCI_USBSTS);
    if (sts & USBSTS_HCH)
    {
        if (xhci_reset_hc(xc) != 0)
        {
            printk("XHCI: reset timeout\n");
            return -1;
        }
    }
    else
    {
        printk("XHCI: HC already running — stopping first\n");
        xhci_write32(xc->op, XHCI_USBCMD, 0);
        if (xhci_wait_halt(xc, 1) != 0)
        {
            printk("XHCI: stop timeout\n");
            return -1;
        }
    }

    if (ring_init(&xc->cmd_ring) != 0)
    {
        printk("XHCI: cmd ring alloc failed\n");
        return -1;
    }

    {
        size_t evt_pages = (TRBS_PER_SEG * TRB_SIZE + PAGE_SIZE - 1) / PAGE_SIZE;
        xc->evt_ring = (xhci_trb_t *)alloc_phys_pages(evt_pages, &xc->evt_ring_phys);
        if (!xc->evt_ring)
        {
            printk("XHCI: evt ring alloc failed\n");
            ring_free(&xc->cmd_ring);
            return -1;
        }
        xc->evt_deq_idx = 0;
        xc->evt_cycle = 1;

        xc->erst = (xhci_erst_entry_t *)alloc_phys_pages(1, &xc->erst_phys);
        if (!xc->erst)
        {
            free_phys_pages(xc->evt_ring, xc->evt_ring_phys, evt_pages);
            ring_free(&xc->cmd_ring);
            return -1;
        }
        xc->erst[0].addr = xc->evt_ring_phys;
        xc->erst[0].size = TRBS_PER_SEG;
    }

    {
        size_t dcbaa_bytes = (size_t)(xc->max_slots + 1) * 8;
        size_t dcbaa_pages = (dcbaa_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
        xc->dcbaa = (uint64_t *)alloc_phys_pages(dcbaa_pages, &xc->dcbaa_phys);
        if (!xc->dcbaa)
        {
            size_t evt_pages = (TRBS_PER_SEG * TRB_SIZE + PAGE_SIZE - 1) / PAGE_SIZE;
            free_phys_pages(xc->evt_ring, xc->evt_ring_phys, evt_pages);
            free_phys_pages(xc->erst, xc->erst_phys, 1);
            ring_free(&xc->cmd_ring);
            return -1;
        }
        memset(xc->dcbaa, 0, dcbaa_bytes);
    }

    xhci_op_write64(xc, XHCI_DCBAAP, xc->dcbaa_phys);
    xhci_write32(xc->op, XHCI_CONFIG, CONFIG_MAX_SLOTS_EN(xc->max_slots));

    {
        volatile uint32_t *ir_base = &xc->rt[XHCI_IR_OFF(0) / 4];
        xhci_write32(ir_base, IR_ERSTSZ, XHCI_ERST_SIZE);
        xhci_write64(ir_base, IR_ERSTBA, xc->erst_phys);
        xhci_write64(ir_base, IR_ERDP, xc->evt_ring_phys);
        xhci_write32(ir_base, IR_IMAN, IMAN_IP | IMAN_IE);
        xhci_write32(ir_base, IR_IMOD, 0);
    }

    xhci_op_write64(xc, XHCI_CRCR, xc->cmd_ring.phys | CRCR_RCS);
    xhci_write32(xc->op, XHCI_USBCMD, USBCMD_RUN | USBCMD_INTE | USBCMD_HSEE);

    if (xhci_wait_halt(xc, 0) != 0)
    {
        printk("XHCI: HC failed to start\n");
        return -1;
    }

    printk("XHCI: HC started\n");

    /* Dump first few event ring TRBs and IMAN */
    volatile uint32_t *ir_base = &xc->rt[XHCI_IR_OFF(0) / 4];
    uint32_t iman = xhci_read32(ir_base, IR_IMAN);
    uint32_t *eraw = (uint32_t *)xc->evt_ring;
    for (int ei = 0; ei < 4; ei++)
        printk("XHCI: evt_ring[%d] = [0x%x,0x%x,0x%x,0x%x]\n",
               ei, eraw[ei*4], eraw[ei*4+1], eraw[ei*4+2], eraw[ei*4+3]);
    printk("XHCI: IMAN=0x%x ERDP=0x%lx\n", iman, (unsigned long)xhci_read64(ir_base, IR_ERDP));

    return 0;
}

/* ------------------------------------------------------------------ *
 * Port operations
 * ------------------------------------------------------------------ */
static void xhci_power_on_ports(struct xhci_controller *xc)
{
    for (int i = 0; i < xc->max_ports; i++)
    {
        volatile uint32_t *portsc = (volatile uint32_t *)((uintptr_t)xc->port + i * XHCI_PORT_REGS_SIZE);
        uint32_t ps = *portsc;
        printk("XHCI: port %d initial ps=0x%x\n", i, ps);
        if (ps & PORTSC_PP)
            continue;
        *portsc = ps | PORTSC_PP;
        __asm__ volatile("mfence" ::: "memory");
        ps = *portsc;
        if (ps & PORTSC_PP)
            printk("XHCI: port %d powered on (ps=0x%x)\n", i, ps);
        else
            printk("XHCI: port %d power-on failed (ps=0x%x)\n", i, ps);
    }
    for (int i = 0; i < 100000; i++)
        __asm__ volatile("pause");
}

static void xhci_print_port_status(struct xhci_controller *xc)
{
    for (int i = 0; i < xc->max_ports; i++)
    {
        uint32_t ps = xhci_port_read(xc, i, 0);
        printk("XHCI: port %d: CCS=%d PED=%d PR=%d PP=%d PLS=%d speed=%d\n",
               i, (ps >> 0) & 1, (ps >> 1) & 1, (ps >> 4) & 1,
               (ps >> 9) & 1, (int)PORTSC_PLS(ps), (int)PORTSC_SPEED(ps));
    }
}

/* ------------------------------------------------------------------ *
 * Device enumeration
 * ------------------------------------------------------------------ */
static struct xhci_device *xhci_get_device(struct xhci_controller *xc, int slot_id)
{
    if (slot_id < 0 || slot_id >= 256)
        return 0;
    struct xhci_device *dev = &xc->devices[slot_id];
    memset(dev, 0, sizeof(*dev));
    dev->slot_id = slot_id;
    return dev;
}

static int xhci_enable_slot(struct xhci_controller *xc, uint32_t *slot_id_out)
{
    xhci_trb_t trb;
    memset(&trb, 0, sizeof(trb));
    trb.flags = TRB_TYPE_ENABLE_SLOT << 10;

    int ret = xhci_send_cmd(xc, &trb);
    if (ret != CC_SUCCESS)
        return ret;

    *slot_id_out = g_cmd_comp.slot_id;
    return CC_SUCCESS;
}

static int xhci_get_root_hub_port(int port_idx)
{
    return port_idx + 1;
}

static int xhci_address_device(struct xhci_controller *xc, struct xhci_device *dev)
{
    int slot_id = dev->slot_id;
    int port = dev->port;
    int speed = dev->speed;

    size_t ctx_sz = xc->context_size;
    size_t input_sz = sizeof(xhci_input_ctrl_t) + ctx_sz * (1 + XHCI_MAX_EPS);
    size_t input_pages = (input_sz + PAGE_SIZE - 1) / PAGE_SIZE;

    uintptr_t in_phys;
    void *in_virt = alloc_phys_pages(input_pages, &in_phys);
    if (!in_virt) return -1;
    memset(in_virt, 0, input_pages * PAGE_SIZE);

    size_t dev_sz = ctx_sz * (1 + XHCI_MAX_EPS);
    size_t dev_pages = (dev_sz + PAGE_SIZE - 1) / PAGE_SIZE;
    uintptr_t dev_phys;
    void *dev_virt = alloc_phys_pages(dev_pages, &dev_phys);
    if (!dev_virt)
    {
        free_phys_pages(in_virt, in_phys, input_pages);
        return -1;
    }
    memset(dev_virt, 0, dev_pages * PAGE_SIZE);

    dev->dev_ctx_phys = dev_phys;
    dev->dev_ctx_virt = dev_virt;
    dev->input_ctx_phys = in_phys;
    dev->input_ctx_virt = in_virt;

    struct xhci_ring *ep0_ring = &dev->ep_rings[1];
    if (ring_init(ep0_ring) != 0)
    {
        free_phys_pages(in_virt, in_phys, input_pages);
        free_phys_pages(dev_virt, dev_phys, dev_pages);
        return -1;
    }

    xc->dcbaa[slot_id] = dev_phys;

    xhci_input_ctrl_t *ctrl = (xhci_input_ctrl_t *)in_virt;
    ctrl->add_flags = (1 << 0) | (1 << 1);

    uint8_t *slot_ctx = (uint8_t *)in_virt + ctx_sz;
    *(uint32_t *)&slot_ctx[0] = SLOT_CTX_CONTEXT_ENTRIES(1) |
                                 SLOT_CTX_SPEED(speed);
    int rh_port = xhci_get_root_hub_port(port);
    *(uint32_t *)&slot_ctx[4] = SLOT_CTX_ROOT_HUB_PORT(rh_port);
    *(uint32_t *)&slot_ctx[8] = SLOT_CTX_INTERRUPTER(0);

    uint8_t *ep0_ctx = slot_ctx + ctx_sz;
    int mps = 8;
    if (speed == SPEED_HIGH || speed == SPEED_FULL) mps = 64;
    else if (speed == SPEED_SUPER) mps = 512;

    *(uint32_t *)&ep0_ctx[0] = EP_CTX_EP_TYPE(XHCI_EP_TYPE_CONTROL) |
                                EP_CTX_MAX_PACKET_SIZE(mps) |
                                EP_CTX_CERR(3);
    *(uint32_t *)&ep0_ctx[4] = EP_CTX_AVG_TRB_LENGTH(8);
    uint64_t ep0_dq = ep0_ring->phys | (ep0_ring->cycle & 1);
    *(uint32_t *)&ep0_ctx[8]  = (uint32_t)(ep0_dq & 0xFFFFFFFF);
    *(uint32_t *)&ep0_ctx[12] = (uint32_t)(ep0_dq >> 32);

    printk("XHCI: in_phys=0x%lx drop=0x%x add=0x%x slot=[0x%x,0x%x,0x%x] ep0=[0x%x,0x%x,0x%lx]\n",
           in_phys, *(uint32_t*)in_virt, *(uint32_t*)((uint8_t*)in_virt + 4),
           *(uint32_t*)slot_ctx, *(uint32_t*)(slot_ctx + 4), *(uint32_t*)(slot_ctx + 8),
           *(uint32_t*)ep0_ctx, *(uint32_t*)(ep0_ctx + 4), ep0_dq);

    xhci_trb_t trb;
    memset(&trb, 0, sizeof(trb));
    trb.ptr = in_phys;
    trb.status = TRB_TYPE_ADDRESS_DEV << 10;
    trb.flags = TRB_IOC | TRB_TYPE_ADDRESS_DEV << 10 |
                TRB_DW2_SLOT(slot_id);

    int ret = xhci_send_cmd(xc, &trb);
    return (ret == CC_SUCCESS) ? 0 : -1;
}

/* ------------------------------------------------------------------ *
 * Port status change -> device enumeration
 * ------------------------------------------------------------------ */
static int xhci_port_has_device(struct xhci_controller *xc, int port)
{
    for (int i = 0; i < 256; i++)
        if (xc->devices[i].slot_id != 0 && xc->devices[i].port == port)
            return 1;
    return 0;
}

static void xhci_check_ports(struct xhci_controller *xc)
{
    for (int p = 0; p < xc->max_ports; p++)
    {
        uint32_t ps = xhci_port_read(xc, p, 0);
        uint32_t changed = ps & PORTSC_CHANGE_BITS;

        if (changed)
            xhci_port_write(xc, p, 0, ps);

        if ((ps & PORTSC_CCS) && !xhci_port_has_device(xc, p))
        {
            printk("XHCI: device connected on port %d (speed=%d)\n",
                   p, (int)PORTSC_SPEED(ps));

            uint32_t slot_id = 0;
            int ret = xhci_enable_slot(xc, &slot_id);
            if (ret != CC_SUCCESS)
            {
                printk("XHCI: Enable Slot failed (cc=%d)\n", ret);
                continue;
            }
            printk("XHCI: slot %u assigned\n", slot_id);

            struct xhci_device *dev = xhci_get_device(xc, (int)slot_id);
            if (!dev)
            {
                printk("XHCI: no free device slot\n");
                continue;
            }

            dev->port = p;
            dev->speed = (int)PORTSC_SPEED(ps);
            dev->address = 0;

            if (xhci_address_device(xc, dev) != 0)
            {
                printk("XHCI: Address Device failed for slot %d\n", slot_id);
                continue;
            }

            dev->address = (uint8_t)slot_id;

            dev->usb_dev.slot_id = slot_id;
            dev->usb_dev.port = p;
            dev->usb_dev.speed = dev->speed;
            dev->usb_dev.address = (uint8_t)slot_id;
            dev->usb_dev.state = USB_STATE_ADDRESS;
            dev->usb_dev.hc_priv = dev;
            dev->usb_dev.ops = &xc->ops;

            struct usb_setup_packet sdp;
            memset(&sdp, 0, sizeof(sdp));
            sdp.bmRequestType = 0x80;
            sdp.bRequest = USB_REQ_GET_DESCRIPTOR;
            sdp.wValue = (USB_DESC_DEVICE << 8) | 0;
            sdp.wLength = sizeof(struct usb_device_descriptor);

            uint8_t dbuf[256];
            memset(dbuf, 0, sizeof(dbuf));
            if (xhci_control_transfer(&dev->usb_dev, &sdp, dbuf,
                                       sizeof(struct usb_device_descriptor)) == 0)
            {
                memcpy(&dev->usb_dev.desc, dbuf, sizeof(dev->usb_dev.desc));
                printk("XHCI: device %x:%x class=%d MPS=%d\n",
                       dev->usb_dev.desc.idVendor,
                       dev->usb_dev.desc.idProduct,
                       dev->usb_dev.desc.bDeviceClass,
                       dev->usb_dev.desc.bMaxPacketSize0);
            }
            else
            {
                printk("XHCI: GET_DESCRIPTOR(DEVICE) failed for slot %d\n", slot_id);
                xc->num_devices++;
                continue;
            }

            sdp.wValue = (USB_DESC_CONFIGURATION << 8) | 0;
            sdp.wLength = 9;
            struct usb_config_descriptor cfg_hdr;
            memset(&cfg_hdr, 0, sizeof(cfg_hdr));
            if (xhci_control_transfer(&dev->usb_dev, &sdp, &cfg_hdr, 9) != 0)
            {
                printk("XHCI: GET_DESCRIPTOR(CONFIG) header failed, staying unconfigured\n");
                xc->num_devices++;
                continue;
            }

            uint16_t total_len = cfg_hdr.wTotalLength;
            if (total_len < 9 || total_len > 512)
                total_len = 9;
            uint8_t *cfg_buf = (uint8_t *)kmalloc(total_len);
            if (!cfg_buf)
            {
                xc->num_devices++;
                continue;
            }

            sdp.wLength = total_len;
            if (xhci_control_transfer(&dev->usb_dev, &sdp, cfg_buf, total_len) != 0)
            {
                printk("XHCI: GET_DESCRIPTOR(CONFIG) full failed\n");
                kfree(cfg_buf);
                xc->num_devices++;
                continue;
            }

            dev->usb_dev.config = (struct usb_config_descriptor *)cfg_buf;
            uint8_t config_val = cfg_buf[5];

            int ep_addrs[16], ep_types[16], ep_mps[16];
            int num_eps = 0;
            uint8_t *p = cfg_buf + 9;
            uint8_t *end = cfg_buf + total_len;
            while (p + 2 <= end && p[0] >= 2 && p + p[0] <= end && num_eps < 16)
            {
                if (p[1] == USB_DESC_ENDPOINT)
                {
                    struct usb_endpoint_descriptor *epd = (struct usb_endpoint_descriptor *)p;
                    ep_addrs[num_eps] = epd->bEndpointAddress;
                    ep_types[num_eps] = EP_ATTR_TYPE(epd->bmAttributes);
                    ep_mps[num_eps] = epd->wMaxPacketSize;
                    num_eps++;
                }
                p += p[0];
            }

            struct usb_setup_packet cfg_req;
            memset(&cfg_req, 0, sizeof(cfg_req));
            cfg_req.bmRequestType = 0x00;
            cfg_req.bRequest = USB_REQ_SET_CONFIGURATION;
            cfg_req.wValue = config_val;
            cfg_req.wIndex = 0;
            cfg_req.wLength = 0;
            if (xhci_control_transfer(&dev->usb_dev, &cfg_req, 0, 0) == 0)
            {
                for (int ei = 0; ei < num_eps; ei++)
                {
                    if (ep_types[ei] == EP_TYPE_CONTROL) continue;
                    int dir_in = (ep_addrs[ei] & 0x80) ? 1 : 0;
                    int xhci_type;
                    if (ep_types[ei] == EP_TYPE_BULK)
                        xhci_type = dir_in ? XHCI_EP_TYPE_BULK_IN : XHCI_EP_TYPE_BULK_OUT;
                    else if (ep_types[ei] == EP_TYPE_INTERRUPT)
                        xhci_type = dir_in ? XHCI_EP_TYPE_INTERRUPT_IN : XHCI_EP_TYPE_INTERRUPT_OUT;
                    else
                        continue;

                    int r = xhci_configure_endpoint(xc, dev, ep_addrs[ei], xhci_type, ep_mps[ei]);
                    printk("XHCI: ep 0x%02x type=%d mps=%d %s\n",
                           ep_addrs[ei], ep_types[ei], ep_mps[ei], r == 0 ? "OK" : "FAIL");
                }

                dev->usb_dev.state = USB_STATE_CONFIGURED;
                printk("XHCI: device configured slot=%d eps=%d\n", slot_id, num_eps);
                usb_msd_probe(&dev->usb_dev);
            }
            else
            {
                printk("XHCI: SET_CONFIGURATION failed, device stays in Address state\n");
            }

            xc->num_devices++;
            printk("XHCI: device enumerated slot=%d port=%d addr=%d\n",
                   slot_id, p, slot_id);
        }
    }
}

/* ------------------------------------------------------------------ *
 * HCD API implementation (control transfer for EP0)
 * ------------------------------------------------------------------ */
static int xhci_control_transfer(struct usb_device *udev,
                                 struct usb_setup_packet *setup,
                                 void *data, size_t data_len)
{
    struct xhci_controller *xc = g_xhc;
    if (!xc) return -1;

    struct xhci_device *dev = &xc->devices[udev->slot_id];

    int dir_in = setup->bmRequestType & 0x80;

    struct xhci_ring *ep0_ring = &dev->ep_rings[1];
    if (!ep0_ring->trbs)
    {
        if (ring_init(ep0_ring) != 0)
            return -1;
    }

    uintptr_t data_phys = 0;
    void *data_virt = 0;
    if (data_len > 0)
    {
        size_t dp = (data_len + PAGE_SIZE - 1) / PAGE_SIZE;
        uintptr_t p;
        void *v = alloc_phys_pages(dp, &p);
        if (!v) return -1;
        if (!dir_in)
            memcpy(v, data, data_len);
        data_phys = p;
        data_virt = v;
    }

    xhci_trb_t setup_trb;
    memset(&setup_trb, 0, sizeof(setup_trb));
    uint32_t sp[2];
    sp[0] = setup->bmRequestType | ((uint32_t)setup->bRequest << 8) |
            ((uint32_t)setup->wValue << 16);
    sp[1] = setup->wIndex | ((uint32_t)setup->wLength << 16);
    memcpy(&setup_trb.ptr, sp, 8);

    int trt = 0;
    if (data_len > 0)
        trt = dir_in ? 3 : 2;
    else
        trt = 0;

    setup_trb.status = 8;
    setup_trb.flags = TRB_TYPE_SETUP_STAGE << 10 |
                      TRB_IDT | ((uint32_t)trt << 16);
    ring_enqueue_one(ep0_ring, &setup_trb);

    if (data_len > 0)
    {
        xhci_trb_t data_trb;
        memset(&data_trb, 0, sizeof(data_trb));
        data_trb.ptr = data_phys;
        data_trb.status = (uint32_t)data_len;
        data_trb.flags = (3UL << 10) |
                         TRB_CHAIN | (dir_in ? TRB_TR_DIR : 0);
        ring_enqueue_one(ep0_ring, &data_trb);
    }

    xhci_trb_t status_trb;
    memset(&status_trb, 0, sizeof(status_trb));
    status_trb.flags = (4UL << 10) | TRB_IOC;
    if (!xc->qemu_workaround && !(setup->bmRequestType & 0x80))
        status_trb.flags |= TRB_ED;
    ring_enqueue_one(ep0_ring, &status_trb);

    g_tr_comp.done = 0;
    __asm__ volatile("mfence");
    xhci_ring_doorbell(xc, dev->slot_id, 1);
    __asm__ volatile("mfence");
    xhci_read32(xc->op, XHCI_USBSTS);
    int timeout = 5000000;
    while (!g_tr_comp.done && timeout--)
    {
        xhci_drain_events(xc);
        __asm__ volatile("pause");
    }
    if (!g_tr_comp.done)
    {
        uint32_t sts = xhci_read32(xc->op, XHCI_USBSTS);
        printk("XHCI: TRB timeout! enq=%d sts=0x%x\n",
               ep0_ring->enq_idx, sts);
    }

    if (data_len > 0 && data_virt)
    {
        if (dir_in)
            memcpy(data, data_virt, data_len);
        size_t dp = (data_len + PAGE_SIZE - 1) / PAGE_SIZE;
        free_phys_pages(data_virt, data_phys, dp);
    }

    return g_tr_comp.done ? 0 : -1;
}

/* ------------------------------------------------------------------ *
 * Endpoint configuration
 * ------------------------------------------------------------------ */
static int xhci_configure_endpoint(struct xhci_controller *xc,
                                    struct xhci_device *dev,
                                    int ep_addr, int ep_type,
                                    int max_packet_size)
{
    int slot_id = dev->slot_id;
    int dci = xhci_ep_dci(ep_addr);
    size_t ctx_sz = xc->context_size;

    struct xhci_ring *ring = xhci_get_ep_ring(dev, ep_addr);
    if (!ring) return -1;
    if (!ring->trbs)
    {
        if (ring_init(ring) != 0)
            return -1;
    }

    memset(dev->input_ctx_virt, 0, sizeof(xhci_input_ctrl_t) + ctx_sz * (1 + XHCI_MAX_EPS));

    xhci_input_ctrl_t *ctrl = (xhci_input_ctrl_t *)dev->input_ctx_virt;
    ctrl->add_flags = (1 << 0) | (1 << dci);

    uint8_t *slot_ctx = (uint8_t *)dev->input_ctx_virt + ctx_sz;
    uint8_t *dev_slot_ctx = (uint8_t *)dev->dev_ctx_virt;
    memcpy(slot_ctx, dev_slot_ctx, ctx_sz);

    uint32_t entries = (*(uint32_t *)&slot_ctx[0] >> 27) & 0x1F;
    if (dci > (int)entries)
    {
        *(uint32_t *)&slot_ctx[0] &= ~(0x1FUL << 27);
        *(uint32_t *)&slot_ctx[0] |= SLOT_CTX_CONTEXT_ENTRIES(dci);
    }

    uint8_t *ep_ctx = (uint8_t *)dev->input_ctx_virt + ctx_sz + (size_t)dci * ctx_sz;
    uint64_t dq = ring->phys | (ring->cycle & 1);

    *(uint32_t *)&ep_ctx[0] = EP_CTX_EP_TYPE(ep_type) |
                               EP_CTX_MAX_PACKET_SIZE(max_packet_size) |
                               EP_CTX_CERR(3);
    *(uint32_t *)&ep_ctx[4] = EP_CTX_AVG_TRB_LENGTH(max_packet_size);
    *(uint32_t *)&ep_ctx[8]  = (uint32_t)(dq & 0xFFFFFFFF);
    *(uint32_t *)&ep_ctx[12] = (uint32_t)(dq >> 32);

    xhci_trb_t trb;
    memset(&trb, 0, sizeof(trb));
    trb.ptr = dev->input_ctx_phys;
    trb.status = TRB_TYPE_CONFIGURE_EP << 10;
    trb.flags = TRB_IOC | TRB_TYPE_CONFIGURE_EP << 10 |
                TRB_DW2_SLOT(slot_id);

    int ret = xhci_send_cmd(xc, &trb);
    if (ret != CC_SUCCESS)
    {
        ring_free(ring);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ *
 * Bulk / Interrupt transfer
 * ------------------------------------------------------------------ */
static int xhci_do_transfer(struct xhci_controller *xc,
                             struct xhci_device *dev,
                             int ep_addr, int ep_type,
                             int max_packet_size,
                             void *data, size_t len)
{
    int dir_in = (ep_addr & 0x80) ? 1 : 0;
    int dci = xhci_ep_dci(ep_addr);

    struct xhci_ring *ring = xhci_get_ep_ring(dev, ep_addr);
    if (!ring) return -1;

    if (!ring->trbs)
    {
        if (ring_init(ring) != 0)
            return -1;
        if (xhci_configure_endpoint(xc, dev, ep_addr, ep_type, max_packet_size) != 0)
        {
            printk("XHCI: failed to configure ep 0x%x\n", ep_addr);
            ring_free(ring);
            return -1;
        }
    }

    uintptr_t data_phys = 0;
    void *data_virt = 0;
    if (len > 0)
    {
        size_t dp = (len + PAGE_SIZE - 1) / PAGE_SIZE;
        uintptr_t p;
        void *v = alloc_phys_pages(dp, &p);
        if (!v) return -1;
        if (!dir_in)
            memcpy(v, data, len);
        data_phys = p;
        data_virt = v;
    }

    size_t remaining = len;
    size_t offset = 0;
    int trb_count = 0;

    while (remaining > 0)
    {
        size_t chunk = remaining;
        if (chunk > 65536) chunk = 65536;

        int is_last = (chunk >= remaining);

        xhci_trb_t trb;
        memset(&trb, 0, sizeof(trb));
        trb.ptr = data_phys + offset;
        trb.status = (uint32_t)chunk;
        trb.flags = (1UL << 10) |
                    (dir_in ? TRB_TR_DIR : 0);
        if (is_last)
        {
            trb.flags |= TRB_IOC;
            if (dir_in)
                trb.flags |= TRB_ISP;
        }
        else
        {
            trb.flags |= TRB_CHAIN;
        }
        ring_enqueue_one(ring, &trb);

        offset += chunk;
        remaining -= chunk;
        trb_count++;
    }

    printk("XHCI: DOORBELL slot=%d dci=%d trb=[0x%lx,0x%x,0x%x] evt_ring flags=0x%x\n",
           dev->slot_id, dci,
           ring->trbs[(ring->enq_idx + TRBS_PER_SEG - 1) % TRBS_PER_SEG].ptr,
           ring->trbs[(ring->enq_idx + TRBS_PER_SEG - 1) % TRBS_PER_SEG].status,
           ring->trbs[(ring->enq_idx + TRBS_PER_SEG - 1) % TRBS_PER_SEG].flags,
           xc->evt_ring[xc->evt_deq_idx].flags);

    g_tr_comp.done = 0;
    xhci_ring_doorbell(xc, dev->slot_id, dci);

    printk("XHCI: post-db evt flags=0x%x\n",
           xc->evt_ring[xc->evt_deq_idx].flags);

    int timeout = 200000000;
    while (!g_tr_comp.done && timeout--)
    {
        int n = xhci_drain_events(xc);
        if (n > 0) {
            printk("XHCI: drained %d events\n", n);
        }
        __asm__ volatile("pause");
    }
    if (!g_tr_comp.done)
    {
        uint32_t sts = xhci_read32(xc->op, XHCI_USBSTS);
        printk("XHCI: TRB timeout! ep=0x%x sts=0x%x\n", ep_addr, sts);
        printk("XHCI: evt flags=0x%x evt_ptr=0x%lx\n",
               xc->evt_ring[xc->evt_deq_idx].flags,
               xc->evt_ring[xc->evt_deq_idx].ptr);
    }
    else
    {
        printk("XHCI: transfer complete!\n");
    }

    if (len > 0 && data_virt)
    {
        if (dir_in)
            memcpy(data, data_virt, len);
        size_t dp = (len + PAGE_SIZE - 1) / PAGE_SIZE;
        free_phys_pages(data_virt, data_phys, dp);
    }

    return g_tr_comp.done ? 0 : -1;
}

static int xhci_bulk_transfer(struct usb_device *udev, int ep_addr,
                               void *data, size_t len)
{
    struct xhci_controller *xc = g_xhc;
    if (!xc) return -1;
    struct xhci_device *dev = &xc->devices[udev->slot_id];

    int dir_in = (ep_addr & 0x80) ? 1 : 0;
    int ep_type = dir_in ? XHCI_EP_TYPE_BULK_IN : XHCI_EP_TYPE_BULK_OUT;
    int mps = 1024;
    if (dev->speed == SPEED_HIGH)
        mps = 512;
    else if (dev->speed == SPEED_FULL)
        mps = 64;

    int ret = xhci_do_transfer(xc, dev, ep_addr, ep_type, mps, data, len);

    if (ret == 0)
        return (int)len;
    return ret;
}

static int xhci_interrupt_transfer(struct usb_device *udev, int ep_addr,
                                    void *data, size_t len)
{
    struct xhci_controller *xc = g_xhc;
    if (!xc) return -1;
    struct xhci_device *dev = &xc->devices[udev->slot_id];

    int dir_in = (ep_addr & 0x80) ? 1 : 0;
    int ep_type = dir_in ? XHCI_EP_TYPE_INTERRUPT_IN : XHCI_EP_TYPE_INTERRUPT_OUT;
    int mps = 1024;
    if (dev->speed == SPEED_HIGH)
        mps = 64;
    else if (dev->speed == SPEED_FULL)
        mps = 8;

    int ret = xhci_do_transfer(xc, dev, ep_addr, ep_type, mps, data, len);

    if (ret == 0)
        return (int)len;
    return ret;
}

/* ------------------------------------------------------------------ *
 * Poll events (called from main loop)
 * ------------------------------------------------------------------ */
void xhci_poll_events(void)
{
    if (g_xhc)
        xhci_check_ports(g_xhc);
}

/* ------------------------------------------------------------------ *
 * Probe function
 * ------------------------------------------------------------------ */
int xhci_probe(struct pci_device *pdev)
{
    if (device_find("usb0"))
        return 0;

    if (pdev->class_code != 0x0C || pdev->subclass != 0x03)
        return 0;

    printk("XHCI: found %x:%x (bus=%d dev=%d func=%d)\n",
           pdev->vendor, pdev->device, pdev->bus, pdev->dev, pdev->func);

    uint64_t bar_phys = pci_bar_addr(pdev, 0);
    if (!bar_phys)
    {
        printk("XHCI: BAR0 is zero\n");
        return -1;
    }
    printk("XHCI: MMIO phys=0x%lx\n", bar_phys);

    uint32_t cmd = pci_read32(pdev->bus, pdev->dev, pdev->func, 0x04);
    cmd |= 6;
    pci_write32(pdev->bus, pdev->dev, pdev->func, 0x04, cmd);

    size_t reg_pages = 16;
    void *mmio_virt = vmm_mmap_phys(bar_phys, reg_pages, PTE_WRITE);
    if (!mmio_virt)
    {
        printk("XHCI: failed to map MMIO\n");
        return -1;
    }

    struct xhci_controller *xc = (struct xhci_controller *)kmalloc(sizeof(struct xhci_controller));
    if (!xc)
    {
        vmm_unmap_page((uintptr_t)mmio_virt);
        return -1;
    }
    memset(xc, 0, sizeof(*xc));

    xc->phys_base = bar_phys;
    xc->capl = (volatile uint8_t *)mmio_virt;
    uint8_t caplen = xhci_read8(xc->capl, XHCI_CAPLENGTH);
    uintptr_t base = (uintptr_t)mmio_virt;
    xc->op  = (volatile uint32_t *)(base + caplen);
    xc->port = (volatile uint32_t *)(base + 0x400);
    uint32_t dboff  = xhci_read32((volatile uint32_t *)xc->capl, XHCI_DBOFF);
    uint32_t rtsoff = xhci_read32((volatile uint32_t *)xc->capl, XHCI_RTSOFF);
    xc->db = (volatile uint32_t *)(base + dboff);
    xc->rt = (volatile uint32_t *)(base + rtsoff);
    xc->pci_dev = pdev;
    xc->irq_vector = 0;

    g_xhc = xc;

    if (xhci_init(xc) != 0)
    {
        printk("XHCI: init failed\n");
        vmm_unmap_page((uintptr_t)mmio_virt);
        kfree(xc);
        return -1;
    }

    xc->qemu_workaround = acpi_oem_is_bochs();
    if (xc->qemu_workaround)
        printk("XHCI: QEMU/BOCHS detected, applying workarounds\n");

    xhci_power_on_ports(xc);
    xhci_print_port_status(xc);

    xhci_setup_msi(xc);

    xc->ops.control_transfer = xhci_control_transfer;
    xc->ops.bulk_transfer = xhci_bulk_transfer;
    xc->ops.interrupt_transfer = xhci_interrupt_transfer;

    struct device *d = (struct device *)kmalloc(sizeof(struct device));
    memset(d, 0, sizeof(*d));
    strcpy(d->name, "usb0");
    d->major = 189; d->minor = 0; d->type = DEV_CHAR;
    d->priv = xc;
    device_register(d);

    printk("XHCI: driver ready\n");

    xhci_check_ports(xc);

    return 0;
}
