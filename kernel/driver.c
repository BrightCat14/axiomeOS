#include "driver.h"
#include "slab.h"
#include "printk.h"
#include "string.h"
#include "io.h"
#include "ide.h"
#include "serial.h"
#include <stddef.h>

static struct driver *g_drivers;
static struct device *g_devices;

static void dname(char *dst, const char *src)
{
    int i = 0;
    for (; src[i] && i < 31; i++)
        dst[i] = src[i];
    dst[i] = 0;
}

/* ---------------------------------------------------------------- *
 * Registry
 * ---------------------------------------------------------------- */
void driver_register(struct driver *d)
{
    d->next = g_drivers;
    g_drivers = d;
}

void driver_unregister(struct driver *d)
{
    if (d == g_drivers)
    {
        g_drivers = d->next;
        d->next = 0;
        return;
    }
    for (struct driver *p = g_drivers; p && p->next; p = p->next)
    {
        if (p->next == d)
        {
            p->next = d->next;
            d->next = 0;
            return;
        }
    }
}

void device_register(struct device *d)
{
    d->next = g_devices;
    g_devices = d;
}

struct device *device_find(const char *name)
{
    for (struct device *d = g_devices; d; d = d->next)
        if (strcmp(d->name, name) == 0)
            return d;
    return 0;
}

int device_enumerate(struct device **out, int max)
{
    int n = 0;
    for (struct device *d = g_devices; d && n < max; d = d->next)
        out[n++] = d;
    return n;
}

/* ---------------------------------------------------------------- *
 * Probe matching
 * ---------------------------------------------------------------- */
int driver_probe_pci(struct pci_device *pdev)
{
    for (struct driver *d = g_drivers; d; d = d->next)
    {
        int v_ok = (d->vendor == DRV_ANY) || (d->vendor == pdev->vendor);
        int d_ok = (d->device == DRV_ANY) || (d->device == pdev->device);
        int c_ok = (d->pci_class == (uint8_t)(DRV_ANY & 0xFF)) ||
                   (d->pci_class == pdev->class_code);
        int s_ok = (d->pci_subclass == (uint8_t)(DRV_ANY & 0xFF)) ||
                   (d->pci_subclass == pdev->subclass);
        if (v_ok && d_ok && c_ok && s_ok)
        {
            printk("DRV: %s bound to %x:%x\n", d->name, (int)pdev->vendor, (int)pdev->device);
            if (d->probe)
                d->probe(pdev);
            return 1;
        }
    }
    return 0;
}

void driver_probe_all(void)
{
    for (struct pci_device *p = pci_first(); p; p = p->next)
        driver_probe_pci(p);
}

/* 12.9 Hotplug: re-enumerate the PCI bus and re-probe. Idempotent because
   every probe guards against double device registration. */
void driver_rescan(void)
{
    pci_init();
    driver_probe_all();
}

/* ---------------------------------------------------------------- *
 * Device operations for built-in / working devices
 * ---------------------------------------------------------------- */
static long zero_read(struct device *d, uint64_t off, void *buf, size_t len)
{
    (void)d; (void)off;
    memset(buf, 0, len);
    return (long)len;
}
static long zero_write(struct device *d, uint64_t off, const void *buf, size_t len)
{
    (void)d; (void)off; (void)buf;
    return (long)len;
}

#define COM1 0x3F8
static long tty_read(struct device *d, uint64_t off, void *buf, size_t len)
{
    (void)d; (void)off;
    size_t got = 0;
    while (got < len)
    {
        if (!(inb(COM1 + 5) & 0x01))
            break;
        ((char *)buf)[got++] = (char)inb(COM1);
    }
    return (long)got;
}
static long tty_write(struct device *d, uint64_t off, const void *buf, size_t len)
{
    (void)d; (void)off;
    for (size_t i = 0; i < len; i++)
    {
        char c = ((const char *)buf)[i];
        if (c == '\n') outb(COM1, '\r');
        outb(COM1, (uint8_t)c);
    }
    return (long)len;
}

/* Block device backed by the legacy ATA driver (ide.c). */
static long ideblk_read(struct device *d, uint64_t off, void *buf, size_t len)
{
    struct block_dev *bd = (struct block_dev *)d->priv;
    if (!bd || !bd->present) return -1;
    uint64_t lba = off >> 9;
    uint32_t nsec = (uint32_t)((len + 511) >> 9);
    uint8_t *tmp = (uint8_t *)kmalloc(nsec * 512);
    if (!tmp) return -1;
    if (blk_read(bd, lba, nsec, tmp) != 0) { kfree(tmp); return -1; }
    size_t got = len; if (got > nsec * 512) got = nsec * 512;
    memcpy(buf, tmp, got);
    kfree(tmp);
    return (long)got;
}
static long ideblk_write(struct device *d, uint64_t off, const void *buf, size_t len)
{
    struct block_dev *bd = (struct block_dev *)d->priv;
    if (!bd || !bd->present) return -1;
    uint64_t lba = off >> 9;
    uint32_t nsec = (uint32_t)((len + 511) >> 9);
    uint8_t *tmp = (uint8_t *)kmalloc(nsec * 512);
    if (!tmp) return -1;
    memcpy(tmp, buf, len);
    if (blk_write(bd, lba, nsec, tmp) != 0) { kfree(tmp); return -1; }
    kfree(tmp);
    return (long)len;
}

/* ---------------------------------------------------------------- *
 * Built-in drivers
 * ---------------------------------------------------------------- */
static int ata_probe(struct pci_device *pdev)
{
    (void)pdev;
    if (device_find("ide0"))
        return 0;
    struct block_dev *bd = ide_get_dev(0, 0);
    if (!bd || !bd->present)
    {
        /* ide_probe may not have been called yet; trigger it. */
        static int tried;
        if (!tried) { ide_probe(0, 0); tried = 1; }
        bd = ide_get_dev(0, 0);
    }
    if (!bd || !bd->present)
        return 0;
    struct device *d = (struct device *)kmalloc(sizeof(struct device));
    memset(d, 0, sizeof(*d));
    dname(d->name, "ide0");
    d->major = 3; d->minor = 0; d->type = DEV_BLOCK;
    d->drv = 0; d->priv = bd;
    d->ops.read = ideblk_read;
    d->ops.write = ideblk_write;
    device_register(d);
    printk("DRV: ide0 -> /dev/ide0 (%lu sectors)\n", (unsigned long)bd->total_sectors);
    return 0;
}

static int stub_probe(struct pci_device *pdev, const char *name)
{
    if (device_find(name))
        return 0;
    struct device *d = (struct device *)kmalloc(sizeof(struct device));
    memset(d, 0, sizeof(*d));
    dname(d->name, name);
    d->type = DEV_BLOCK;
    d->ops.read = 0; d->ops.write = 0;
    device_register(d);
    printk("DRV: %s -> /dev/%s (ops not implemented)\n", name, name);
    return 0;
}

static int xhci_probe(struct pci_device *pdev)
{ return stub_probe(pdev, "usb0"); }
static int vga_probe(struct pci_device *pdev)
{ return stub_probe(pdev, "fb0"); }

/* NOTE: the NVMe driver ships as a loadable module (kernel/modules/nvme.kxt)
   and is loaded at runtime via the .kxt framework (kxtload).  The built-in
   stub that used to claim class 0x01/0x08 was removed so the real driver is
   the sole owner of any NVMe controller. */

static struct driver ata_drv = {
    .name = "ata-ide", .vendor = DRV_ANY, .device = DRV_ANY,
    .pci_class = 0x01, .pci_subclass = 0x01, .probe = ata_probe,
};
static struct driver xhci_drv = {
    .name = "xhci", .vendor = DRV_ANY, .device = DRV_ANY,
    .pci_class = 0x0C, .pci_subclass = 0x03, .probe = xhci_probe,
};
static struct driver vga_drv = {
    .name = "vga", .vendor = 0x1234, .device = 0x1111,
    .pci_class = DRV_ANY, .pci_subclass = DRV_ANY, .probe = vga_probe,
};

/* NOTE: the e1000 NIC driver is no longer compiled into the kernel. It ships
   as a loadable module (kernel/modules/e1000.kxt) and is loaded at runtime
   via the .kxt framework (insmod/kxtload). See module.h / module.c. */

/* ---------------------------------------------------------------- *
 * Bootstrap
 * ---------------------------------------------------------------- */
void driver_init(void)
{
    driver_register(&ata_drv);
    driver_register(&xhci_drv);
    driver_register(&vga_drv);

    /* Static character devices (no PCI dependence). */
    struct device *z = (struct device *)kmalloc(sizeof(struct device));
    memset(z, 0, sizeof(*z));
    dname(z->name, "zero");
    z->major = 1; z->minor = 3; z->type = DEV_CHAR;
    z->ops.read = zero_read; z->ops.write = zero_write;
    device_register(z);

    struct device *t = (struct device *)kmalloc(sizeof(struct device));
    memset(t, 0, sizeof(*t));
    dname(t->name, "ttyS0");
    t->major = 4; t->minor = 0; t->type = DEV_CHAR;
    t->ops.read = tty_read; t->ops.write = tty_write;
    device_register(t);

    printk("DRV: framework initialized\n");

    /* Probe drivers against the devices enumerated by pci_init(). */
    driver_probe_all();
}
