#!/usr/bin/env python3
"""Write an MBR partition table to a disk image.

Usage: mkpart.py <disk.img>

The image must already exist (e.g. created with `dd`). Two partitions are
written:
  * Partition 1: FAT32 "BOOT" (type 0x0C), LBA 2048, 130024 sectors (~63.5MB)
  * Partition 2: axiomefs "ROOT" (type 0x83), LBA 131072, 393216 sectors (192MB)

See docs/user-rank-system-spec.md section 11.3.
"""
import struct
import sys


def write_mbr(img_path):
    with open(img_path, 'r+b') as f:
        img = bytearray(f.read())

        # MBR boot signature.
        img[510] = 0x55
        img[511] = 0xAA

        # Partition 1: FAT32, LBA 2048, 130024 sectors (63.5MB).
        off = 446
        struct.pack_into('<BBBBBBBB', img, off,
            0x00,            # status (not bootable)
            0x20, 0x00, 0x00,  # CHS of first sector
            0x0C,            # type: FAT32 LBA
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
