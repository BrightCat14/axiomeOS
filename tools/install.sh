#!/usr/bin/env bash
set -euo pipefail

DEV="${1:-}"
BUILD_DIR="${2:-build}"

if [ -z "$DEV" ]; then
    echo "Usage: sudo $0 /dev/sdX [build_dir]"
    echo "Example: sudo $0 /dev/sdd"
    exit 1
fi

if [ ! -b "$DEV" ]; then
    echo "ERROR: $DEV is not a block device"
    exit 1
fi

DISK_IMG="$BUILD_DIR/disk.img"
KERNEL_ELF="$BUILD_DIR/kernel/kernel.elf"

if [ ! -f "$DISK_IMG" ]; then
    echo "ERROR: $DISK_IMG not found. Run 'make disk.img' first."
    exit 1
fi

if [ ! -f "$KERNEL_ELF" ]; then
    echo "ERROR: $KERNEL_ELF not found. Run 'make kernel' first."
    exit 1
fi

echo "=== axiomeOS Installer (UEFI) ==="
echo "Target device: $DEV"
echo ""
echo "WARNING: This will DESTROY ALL DATA on $DEV!"
read -r -p "Are you sure? (type YES to continue): " confirm
if [ "$confirm" != "YES" ]; then
    echo "Aborted."
    exit 1
fi

# Partition layout (no overlaps!):
#   LBA 0        - GPT protective MBR
#   LBA 1..33    - GPT headers
#   LBA 2048     - Partition 1: EFI System Partition (FAT32, 63 MiB, 129024 sectors)
#   LBA 131072   - Partition 2: axiomefs root (192 MiB, 393216 sectors)
#   LBA 524288   - end

echo ""
echo "Step 1: Writing disk image to $DEV (partition data)..."
dd if="$DISK_IMG" of="$DEV" bs=1M status=progress conv=fsync
sync

echo ""
echo "Step 2: Creating GPT partition table..."
umount "${DEV}1" 2>/dev/null || true
umount "${DEV}2" 2>/dev/null || true

parted -s "$DEV" mklabel gpt
parted -s "$DEV" unit s mkpart primary fat32 2048 131071
parted -s "$DEV" unit s mkpart primary 131072 524287
parted -s "$DEV" set 1 esp on
sync
sleep 1

echo ""
echo "Step 3: Creating fresh FAT32 boot image..."
BOOT_IMG=$(mktemp /tmp/axiome-boot-XXXXXX.img)
trap 'rm -f "$BOOT_IMG"' EXIT
dd if=/dev/zero of="$BOOT_IMG" bs=512 count=129024 status=none
mkfs.vfat -F 32 -n BOOT "$BOOT_IMG"

echo ""
echo "Step 4: Mounting boot image via loopback..."
MNT=$(mktemp -d)
mount -t vfat -o rw,umask=000,flush "$BOOT_IMG" "$MNT"
trap 'umount "$MNT" 2>/dev/null; rmdir "$MNT" 2>/dev/null; rm -f "$BOOT_IMG"' EXIT

echo ""
echo "Step 5: Copying kernel to /boot/..."
mkdir -p "$MNT/boot"
cp "$KERNEL_ELF" "$MNT/boot/kernel.elf"

echo ""
echo "Step 6: Installing GRUB (UEFI only)..."
mkdir -p "$MNT/boot/grub"
if [ -d /usr/lib/grub/x86_64-efi ]; then
    grub-install --target=x86_64-efi \
        --boot-directory="$MNT/boot" \
        --efi-directory="$MNT" \
        --removable --no-nvram
    echo "  UEFI GRUB installed."
else
    echo "  ERROR: GRUB x86_64-efi modules not found."
    exit 1
fi

echo ""
echo "Step 7: Copying GRUB config..."
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
cp "$REPO_ROOT/grub.cfg" "$MNT/boot/grub/"
echo "  grub.cfg copied."

echo ""
echo "Step 8: Verifying..."
if [ -f "$MNT/EFI/BOOT/BOOTX64.EFI" ]; then
    echo "  BOOTX64.EFI found OK"
else
    echo "  WARNING: BOOTX64.EFI not found!"
fi

echo ""
echo "Step 9: Unmounting loopback..."
sync
umount "$MNT"
rmdir "$MNT"

echo ""
echo "Step 10: Writing boot image to $DEV at LBA 2048..."
dd if="$BOOT_IMG" of="$DEV" bs=512 seek=2048 status=progress conv=notrunc,fsync
sync
rm -f "$BOOT_IMG"
trap '' EXIT

echo ""
echo "=== Install complete! ==="
echo "axiomeOS written to $DEV (GPT, UEFI)"
echo ""
echo "Boot your target system from this USB drive."
echo "Make sure Secure Boot is disabled or enroll the GRUB shim."
