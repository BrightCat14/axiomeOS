REPO_ROOT := $(realpath .)
BUILD_DIR := $(REPO_ROOT)/build

PYTHON := python3
HOSTCC := gcc

.PHONY: all kernel iso run run-fb debug clean distclean disk.img

all: iso

kernel:
	$(MAKE) -C kernel BUILD_DIR=$(BUILD_DIR)/kernel

# Host-side axiomefs formatter (enhanced to take a disk image + offset).
$(BUILD_DIR)/kernel/mkfs_axiomefs: tools/mkfs_axiomefs.c
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -I $(REPO_ROOT) -o $@ $<

iso: kernel
	mkdir -p $(BUILD_DIR)/isowork/boot/grub
	cp $(BUILD_DIR)/kernel/kernel.elf $(BUILD_DIR)/isowork/boot/
	cp grub.cfg $(BUILD_DIR)/isowork/boot/grub/
	PATH="/tmp/opencode/mtools-install/bin:$$PATH" grub-mkrescue -o $(BUILD_DIR)/axiome.iso $(BUILD_DIR)/isowork 2>/dev/null

# Build a 256MB MBR disk with:
#   * partition 0: FAT32 "BOOT" (LBA 2048, 130024 sectors) holding kernel.elf
#   * partition 1: axiomefs "ROOT" (LBA 131072) populated from root_manifest.txt
disk.img: kernel $(BUILD_DIR)/kernel/mkfs_axiomefs
	@mkdir -p $(BUILD_DIR)
	dd if=/dev/zero of=$(BUILD_DIR)/disk.img bs=1M count=256
	$(PYTHON) tools/mkpart.py $(BUILD_DIR)/disk.img
	# Format the BOOT partition as FAT32, constrained to its 130024-sector region.
	dd if=/dev/zero of=$(BUILD_DIR)/boot.fat bs=512 count=130024
	mformat -F -i $(BUILD_DIR)/boot.fat -v BOOT ::
	mcopy -i $(BUILD_DIR)/boot.fat $(BUILD_DIR)/kernel/kernel.elf ::kernel.elf
	dd if=$(BUILD_DIR)/boot.fat of=$(BUILD_DIR)/disk.img bs=512 seek=2048 conv=notrunc
	# Format the ROOT partition as axiomefs and populate from the manifest.
	$(BUILD_DIR)/kernel/mkfs_axiomefs $(BUILD_DIR)/disk.img 131072 root_manifest.txt
	rm -f $(BUILD_DIR)/boot.fat

run: iso disk.img
	qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd \
		-cdrom $(BUILD_DIR)/axiome.iso \
		-drive file=$(BUILD_DIR)/disk.img,format=raw,if=ide,index=0,media=disk \
		-m 512M -serial stdio -display sdl \
		-netdev user,id=net0 \
		-device e1000,netdev=net0

run-fb: iso disk.img
	qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd \
		-cdrom $(BUILD_DIR)/axiome.iso \
		-drive file=$(BUILD_DIR)/disk.img,format=raw,if=ide,index=0,media=disk \
		-m 512M -serial stdio -vga std -display sdl

debug: iso disk.img
	qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd \
		-cdrom $(BUILD_DIR)/axiome.iso \
		-drive file=$(BUILD_DIR)/disk.img,format=raw,if=ide,index=0,media=disk \
		-m 512M -serial stdio -s -S

clean:
	rm -rf $(BUILD_DIR)/isodir $(BUILD_DIR)/boot.fat
	$(MAKE) -C kernel clean BUILD_DIR=$(BUILD_DIR)/kernel

distclean:
	rm -rf $(BUILD_DIR)
