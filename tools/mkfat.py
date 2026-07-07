#!/usr/bin/env python3
"""Create a minimal FAT12 image for GRUB EFI boot."""
import struct, sys, os, shutil

OUT = sys.argv[1]
EFI_DIR = sys.argv[2]  # dir containing boot/bootx64.efi

def align_up(x, a):
    return (x + a - 1) & ~(a - 1)

# FAT12 parameters
BYTES_PER_SECTOR = 512
SECTORS_PER_CLUSTER = 1
RESERVED_SECTORS = 1
FAT_COUNT = 2
ROOT_ENTRIES = 224
TOTAL_SECTORS = 2880  # 1.44MB floppy size
MEDIA = 0xF0
SECTORS_PER_FAT = 9
SECTORS_PER_TRACK = 18
HEADS = 2
HIDDEN_SECTORS = 0

root_dir_sectors = (ROOT_ENTRIES * 32 + BYTES_PER_SECTOR - 1) // BYTES_PER_SECTOR
data_sector = RESERVED_SECTORS + FAT_COUNT * SECTORS_PER_FAT + root_dir_sectors
total_clusters = (TOTAL_SECTORS - data_sector) // SECTORS_PER_CLUSTER

img = bytearray(TOTAL_SECTORS * BYTES_PER_SECTOR)

# BPB
def w16(off, v): struct.pack_into('<H', img, off, v)
def w32(off, v): struct.pack_into('<I', img, off, v)

img[0:3] = b'\xEB\x3C\x90'
img[3:11] = b'AXIOME  '
w16(11, BYTES_PER_SECTOR)
img[13] = SECTORS_PER_CLUSTER
w16(14, RESERVED_SECTORS)
img[16] = FAT_COUNT
w16(17, ROOT_ENTRIES)
w16(19, TOTAL_SECTORS)
img[21] = MEDIA
w16(22, SECTORS_PER_FAT)
w16(24, SECTORS_PER_TRACK)
w16(26, HEADS)
w32(28, HIDDEN_SECTORS)
w32(32, 0)  # total sectors for large

# Extended BPB
img[36] = 0x00  # drive number
img[37] = 0x00  # reserved
img[38] = 0x29  # boot signature
w32(39, 0x12345678)
img[43:54] = b'AXIOMEOS  '
img[54:62] = b'FAT12   '

# FATs: cluster 0 = media, cluster 1 = EOC
img[RESERVED_SECTORS * BYTES_PER_SECTOR] = MEDIA
img[RESERVED_SECTORS * BYTES_PER_SECTOR + 1] = 0xFF
img[RESERVED_SECTORS * BYTES_PER_SECTOR + 2] = 0xFF
img[RESERVED_SECTORS * BYTES_PER_SECTOR + 3] = 0xFF

# Copy FAT1 to FAT2
fat1_off = RESERVED_SECTORS * BYTES_PER_SECTOR
fat2_off = (RESERVED_SECTORS + SECTORS_PER_FAT) * BYTES_PER_SECTOR
img[fat2_off:fat2_off + SECTORS_PER_FAT * BYTES_PER_SECTOR] = img[fat1_off:fat1_off + SECTORS_PER_FAT * BYTES_PER_SECTOR]

# Add files to root directory
def add_file(name, content):
    global img, next_cluster
    # Find a free root entry
    root_off = (RESERVED_SECTORS + FAT_COUNT * SECTORS_PER_FAT) * BYTES_PER_SECTOR
    for i in range(ROOT_ENTRIES):
        entry_off = root_off + i * 32
        if img[entry_off] == 0 or img[entry_off] == 0xE5:
            break
    else:
        raise RuntimeError("No free root entry")

    name = name.upper().encode('ascii')
    if b'.' in name:
        base, ext = name.split(b'.')
        base = base[:8].ljust(8, b' ')
        ext = ext[:3].ljust(3, b' ')
    else:
        base = name[:8].ljust(8, b' ')
        ext = b'   '

    img[entry_off:entry_off+8] = base
    img[entry_off+8:entry_off+11] = ext
    img[entry_off+11] = 0x20  # archive
    w16(entry_off+28, 0)  # write time
    w16(entry_off+22, 0)  # create time
    w16(entry_off+24, 0)  # create date
    w16(entry_off+26, 0)  # access date

    # Allocate clusters
    clusters_needed = (len(content) + BYTES_PER_SECTOR - 1) // BYTES_PER_SECTOR
    cluster = 2
    allocated = []
    while len(allocated) < clusters_needed:
        if cluster >= 2 + total_clusters:
            raise RuntimeError("Disk full")
        allocated.append(cluster)
        cluster += 1
    for i, c in enumerate(allocated):
        next_c = allocated[i+1] if i+1 < len(allocated) else 0xFFF
        fat_off = (RESERVED_SECTORS * BYTES_PER_SECTOR) + (c * 3 // 2)
        if c % 2 == 0:
            val = (img[fat_off] | (img[fat_off+1] << 8) & 0x0FFF) | (next_c & 0x0FFF)
            struct.pack_into('<H', img, fat_off, val)
        else:
            val = ((img[fat_off+1] << 8 | img[fat_off]) & 0xF000) | ((next_c << 4) & 0xFF00) | (next_c >> 8)
            img[fat_off] = val & 0xFF

    w16(entry_off+26, allocated[0])  # first cluster
    w32(entry_off+28, len(content))  # file size

    # Write file data
    data_off = data_sector * BYTES_PER_SECTOR
    for i, c in enumerate(allocated):
        chunk = content[i * BYTES_PER_SECTOR:(i+1) * BYTES_PER_SECTOR]
        cluster_off = data_off + (c - 2) * BYTES_PER_SECTOR
        img[cluster_off:cluster_off+len(chunk)] = chunk

os.makedirs(EFI_DIR, exist_ok=True)

# Copy bootx64.efi
grub_efi = '/usr/lib/grub/x86_64-efi-signed/grubnetx64.efi.signed'
if os.path.exists(grub_efi):
    with open(grub_efi, 'rb') as f:
        efi_data = f.read()
else:
    # Build minimal GRUB EFI image manually
    import subprocess
    subprocess.run(['grub-mkimage', '-o', '/tmp/opencode/bootx64.efi',
                    '-p', '/boot/grub', '-O', 'x86_64-efi',
                    'fat', 'part_gpt', 'normal', 'configfile', 'boot',
                    'multiboot2', 'gfxterm', 'gfxmenu'], check=True)
    with open('/tmp/opencode/bootx64.efi', 'rb') as f:
        efi_data = f.read()
    shutil.copy('/tmp/opencode/bootx64.efi', os.path.join(EFI_DIR, 'bootx64.efi'))

add_file('bootx64.efi', efi_data)

with open(OUT, 'wb') as f:
    f.write(img)

print(f"Created FAT image: {OUT} ({len(img)} bytes)")
