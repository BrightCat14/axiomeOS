#!/usr/bin/env python3
"""Write an MBR partition table to a disk image.

Usage: mkpart.py <disk.img>

The image must already exist (e.g. created with `dd`). Two partitions are
written:
  * Partition 1: FAT32 "BOOT" (type 0xEF), LBA 2048, 130024 sectors (~63.5MB)
  * Partition 2: axiomefs "ROOT" (type 0x83), LBA 131072, 393216 sectors (192MB)

Partition 1 is type 0xEF (EFI System Partition) so UEFI firmware
recognises it and scans for /EFI/BOOT/BOOTX64.EFI.  The GRUB bootloader
is installed there by the install target.
"""
import struct
import sys


def write_mbr(img_path):
    with open(img_path, 'r+b') as f:
        img = bytearray(f.read())

        # MBR boot signature.
        img[510] = 0x55
        img[511] = 0xAA

        # Partition 1: EFI System Partition, LBA 2048, 130024 sectors (63.5MB).
        off = 446
        struct.pack_into('<BBBBBBBB', img, off,
            0x80,            # status (bootable)
            0x20, 0x00, 0x00,  # CHS of first sector
            0xEF,            # type: EFI System Partition
            0xFF, 0xFF, 0xFF,  # CHS of last sector
        )
        struct.pack_into('<II', img, off + 8, 2048, 130024)

        # Partition 2: axiomefs, LBA 131072, 393216 sectors (192MB).
        off = 462
        struct.pack_into('<BBBBBBBB', img, off,
            0x00,            # status
            0x00, 0x00, 0x00,
            0x83,            # type: Linux
            0x00, 0x00, 0x00,
        )
        struct.pack_into('<II', img, off + 8, 131072, 393216)

        f.seek(0)
        f.write(img)

    print("wrote MBR partition table to %s" % img_path)


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("usage: mkpart.py <disk.img>")
        sys.exit(1)
    write_mbr(sys.argv[1])
