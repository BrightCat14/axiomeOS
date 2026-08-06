#include "usb_msd.h"
#include "usb.h"
#include "printk.h"
#include "string.h"
#include "slab.h"
#include "driver.h"

static int g_msd_count;

static int ums_send_cbw(struct ums_device *ums, uint8_t flags,
                         uint32_t data_len, uint8_t *cdb, int cdb_len)
{
    struct cbw cbw;
    memset(&cbw, 0, sizeof(cbw));
    cbw.signature = CBW_SIGNATURE;
    cbw.tag = ++ums->tag;
    cbw.data_transfer_len = data_len;
    cbw.flags = flags;
    cbw.lun = 0;
    cbw.cb_len = (uint8_t)cdb_len;
    memcpy(cbw.cdb, cdb, (size_t)cdb_len);

    struct usb_device *dev = ums->usb_dev;
    int ret = dev->ops->bulk_transfer(dev, ums->bulk_out_ep, &cbw, sizeof(cbw));
    if (ret < 0)
    {
        printk("UMS: CBW send failed\n");
        return -1;
    }
    return 0;
}

static int ums_recv_csw(struct ums_device *ums, struct csw *csw_out)
{
    struct csw csw;
    memset(&csw, 0, sizeof(csw));
    struct usb_device *dev = ums->usb_dev;
    int ret = dev->ops->bulk_transfer(dev, ums->bulk_in_ep, &csw, sizeof(csw));
    if (ret < 0)
    {
        printk("UMS: CSW recv failed\n");
        return -1;
    }
    if (csw.signature != CSW_SIGNATURE)
    {
        printk("UMS: bad CSW sig 0x%x\n", csw.signature);
        return -1;
    }
    if (csw.status != 0)
    {
        printk("UMS: CSW status=%d\n", csw.status);
        return -1;
    }
    if (csw_out)
        *csw_out = csw;
    return 0;
}

static int ums_transport(struct ums_device *ums, uint8_t *cdb, int cdb_len,
                          void *data, uint32_t data_len, uint8_t dir_in)
{
    uint8_t flags = dir_in ? 0x80 : 0x00;
    if (ums_send_cbw(ums, flags, data_len, cdb, cdb_len) != 0)
        return -1;

    if (data_len > 0 && data)
    {
        int ep = dir_in ? ums->bulk_in_ep : ums->bulk_out_ep;
        struct usb_device *dev = ums->usb_dev;
        int ret = dev->ops->bulk_transfer(dev, ep, data, data_len);
        if (ret < 0)
        {
            printk("UMS: data xfer failed on ep 0x%x\n", ep);
            return -1;
        }
    }

    return 0;
}

static int ums_read_capacity(struct ums_device *ums)
{
    uint8_t cdb[10];
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = SCSI_READ_CAPACITY_10;

    uint8_t buf[8];
    memset(buf, 0, sizeof(buf));

    if (ums_transport(ums, cdb, 10, buf, 8, 1) != 0)
    {
        printk("UMS: READ_CAPACITY_10 failed\n");
        return -1;
    }

    uint32_t last_lba = ((uint32_t)buf[0] << 24) |
                        ((uint32_t)buf[1] << 16) |
                        ((uint32_t)buf[2] << 8)  |
                        (uint32_t)buf[3];
    uint32_t block_size = ((uint32_t)buf[4] << 24) |
                          ((uint32_t)buf[5] << 16) |
                          ((uint32_t)buf[6] << 8)  |
                          (uint32_t)buf[7];

    ums->total_blocks = (uint64_t)last_lba + 1;
    ums->block_size = block_size;

    printk("UMS: capacity %lu sectors of %u bytes\n",
           (unsigned long)ums->total_blocks, ums->block_size);
    return 0;
}

static int ums_read_10(struct ums_device *ums, uint64_t lba,
                        uint32_t nblocks, void *buf)
{
    uint8_t cdb[10];
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = SCSI_READ_10;
    cdb[2] = (uint8_t)(lba >> 24);
    cdb[3] = (uint8_t)(lba >> 16);
    cdb[4] = (uint8_t)(lba >> 8);
    cdb[5] = (uint8_t)(lba);
    cdb[7] = (uint8_t)(nblocks >> 8);
    cdb[8] = (uint8_t)(nblocks);

    uint32_t data_len = nblocks * ums->block_size;
    return ums_transport(ums, cdb, 10, buf, data_len, 1);
}

static int ums_write_10(struct ums_device *ums, uint64_t lba,
                         uint32_t nblocks, const void *buf)
{
    uint8_t cdb[10];
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = SCSI_WRITE_10;
    cdb[2] = (uint8_t)(lba >> 24);
    cdb[3] = (uint8_t)(lba >> 16);
    cdb[4] = (uint8_t)(lba >> 8);
    cdb[5] = (uint8_t)(lba);
    cdb[7] = (uint8_t)(nblocks >> 8);
    cdb[8] = (uint8_t)(nblocks);

    uint32_t data_len = nblocks * ums->block_size;
    return ums_transport(ums, cdb, 10, (void *)buf, data_len, 0);
}

static long umsblk_read(struct device *d, uint64_t off, void *buf, size_t len)
{
    struct ums_device *ums = (struct ums_device *)d->priv;
    if (!ums) return -1;

    uint64_t lba = off / ums->block_size;
    uint32_t nblocks = (uint32_t)((len + ums->block_size - 1) / ums->block_size);

    uint8_t *tmp = (uint8_t *)kmalloc(nblocks * ums->block_size);
    if (!tmp) return -1;

    if (ums_read_10(ums, lba, nblocks, tmp) != 0)
    {
        kfree(tmp);
        return -1;
    }

    size_t got = len < (size_t)nblocks * ums->block_size
                 ? len : (size_t)nblocks * ums->block_size;
    memcpy(buf, tmp, got);
    kfree(tmp);
    return (long)got;
}

static long umsblk_write(struct device *d, uint64_t off, const void *buf, size_t len)
{
    struct ums_device *ums = (struct ums_device *)d->priv;
    if (!ums) return -1;

    uint64_t lba = off / ums->block_size;
    uint32_t nblocks = (uint32_t)((len + ums->block_size - 1) / ums->block_size);

    uint8_t *tmp = (uint8_t *)kmalloc(nblocks * ums->block_size);
    if (!tmp) return -1;

    memcpy(tmp, buf, len);
    int ret = ums_write_10(ums, lba, nblocks, tmp);
    kfree(tmp);

    return (ret == 0) ? (long)len : -1;
}

int usb_msd_probe(struct usb_device *dev)
{
    if (dev->state != USB_STATE_CONFIGURED)
        return -1;

    struct usb_config_descriptor *cfg = dev->config;
    if (!cfg)
        return -1;

    int found_msd = 0;
    int bulk_in = 0, bulk_out = 0;
    int eps_found = 0;
    int max_eps = 0;

    uint8_t *p = (uint8_t *)cfg;
    uint8_t *end = p + cfg->wTotalLength;

    {
        printk("UMS: raw cfg len=%u", cfg->wTotalLength);
        int dump_len = cfg->wTotalLength < 64 ? cfg->wTotalLength : 64;
        for (int di = 0; di < dump_len; di++)
        {
            if ((di % 16) == 0) printk("\nUMS:  ");
            printk(" %x", p[di]);
        }
        printk("\n");
    }

    while (p + 2 <= end && p[0] >= 2 && p + p[0] <= end)
    {
        if (p[1] == USB_DESC_INTERFACE)
        {
            struct usb_interface_descriptor *iface =
                (struct usb_interface_descriptor *)p;
            if (iface->bInterfaceClass == USB_CLASS_MASS_STORAGE &&
                iface->bInterfaceSubClass == US_SUBCLASS_SCSI &&
                iface->bInterfaceProtocol == US_PROTOCOL_BOT)
            {
                found_msd = 1;
                max_eps = iface->bNumEndpoints;
                eps_found = 0;
            }
            else if (!found_msd)
            {
                max_eps = 0;
            }
        }
        else if (p[1] == USB_DESC_ENDPOINT && found_msd && eps_found < max_eps)
        {
            struct usb_endpoint_descriptor *ep =
                (struct usb_endpoint_descriptor *)p;
            int type = ep->bmAttributes & 3;
            if (type == EP_TYPE_BULK)
            {
                if (ep->bEndpointAddress & 0x80)
                    bulk_in = ep->bEndpointAddress;
                else
                    bulk_out = ep->bEndpointAddress;
                eps_found++;
            }
        }
        p += p[0];
    }

    printk("UMS: total_len=%u found_msd=%d bulk_in=0x%x bulk_out=0x%x eps_found=%d max_eps=%d\n",
           cfg->wTotalLength, found_msd, bulk_in, bulk_out, eps_found, max_eps);

    if (!found_msd || !bulk_in || !bulk_out)
        return -1;

    printk("UMS: found BOT device slot=%d bulk_in=0x%x bulk_out=0x%x\n",
           dev->slot_id, bulk_in, bulk_out);

    struct ums_device *ums = (struct ums_device *)kmalloc(sizeof(struct ums_device));
    if (!ums) return -1;
    memset(ums, 0, sizeof(*ums));
    ums->usb_dev = dev;
    ums->bulk_in_ep = bulk_in;
    ums->bulk_out_ep = bulk_out;
    ums->tag = 0;

    if (ums_read_capacity(ums) != 0)
    {
        kfree(ums);
        return -1;
    }

    char name[8];
    name[0] = 's';
    name[1] = 'd';
    name[2] = (char)('a' + g_msd_count);
    name[3] = 0;

    struct device *d = (struct device *)kmalloc(sizeof(struct device));
    if (!d) { kfree(ums); return -1; }
    memset(d, 0, sizeof(*d));
    strcpy(d->name, name);
    d->major = 8;
    d->minor = g_msd_count * 16;
    d->type = DEV_BLOCK;
    d->drv = 0;
    d->priv = ums;
    d->ops.read = umsblk_read;
    d->ops.write = umsblk_write;
    device_register(d);

    printk("UMS: /dev/%s registered (%lu sectors of %u bytes)\n",
           name, (unsigned long)ums->total_blocks, ums->block_size);
    g_msd_count++;
    return 0;
}
