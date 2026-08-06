#ifndef AXIOME_USB_MSD_H
#define AXIOME_USB_MSD_H

#include <stdint.h>
#include "usb.h"

#define USB_CLASS_MASS_STORAGE  0x08
#define US_SUBCLASS_SCSI        0x06
#define US_PROTOCOL_BOT         0x50

struct __attribute__((packed)) cbw {
    uint32_t signature;
    uint32_t tag;
    uint32_t data_transfer_len;
    uint8_t  flags;
    uint8_t  lun;
    uint8_t  cb_len;
    uint8_t  cdb[16];
};

struct __attribute__((packed)) csw {
    uint32_t signature;
    uint32_t tag;
    uint32_t data_residue;
    uint8_t  status;
};

#define CBW_SIGNATURE  0x43425355UL
#define CSW_SIGNATURE  0x53425355UL

#define SCSI_READ_CAPACITY_10   0x25
#define SCSI_READ_10            0x28
#define SCSI_WRITE_10           0x2A

struct ums_device {
    struct usb_device *usb_dev;
    int bulk_in_ep;
    int bulk_out_ep;
    uint32_t block_size;
    uint64_t total_blocks;
    uint32_t tag;
};

int usb_msd_probe(struct usb_device *dev);

#endif
