REPO_ROOT := $(realpath .)
BUILD_DIR := $(REPO_ROOT)/build

PYTHON := python3
HOSTCC := gcc

DEV ?= /dev/sdx

# Partition geometry (must stay in sync with tools/mkpart.py).
BOOT_PART_LBA     := 2048
BOOT_PART_SECTORS := 129024
ROOT_PART_LBA     := 131072
ROOT_PART_SECTORS := 393216
DISK_SECTORS      := 524288          # 256 MiB

# Each partition is built as its own artifact, so `make disk.img` only re-runs
# the steps whose inputs actually changed instead of reformatting the whole
# disk from scratch on every edit. disk.img itself is assembled in place with
# `dd conv=notrunc`, so untouched partitions (and any runtime state on them,
# e.g. a user created by the first-boot OOBE) are preserved.
BOOT_FAT  := $(BUILD_DIR)/boot.fat
ROOT_AXFS := $(BUILD_DIR)/root.axfs
DISK_PATH := $(BUILD_DIR)/disk.img

# Host files embedded verbatim via `bin:` lines in root_manifest.txt. When any
# of them changes (a userspace .elf or a .kxt), the ROOT partition rebuilds.
MANIFEST_BINS := $(shell sed -n 's/.*[[:space:]]bin:\([^[:space:]]*\).*/\1/p' root_manifest.txt)

.PHONY: all kernel iso run run-fb run-usb debug test-hid clean distclean install disk.img

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
	grub-mkrescue -o $(BUILD_DIR)/axiome.iso $(BUILD_DIR)/isowork 2>/dev/null

# BOOT partition: a FAT32 image holding kernel.elf. Rebuilt only when the
# kernel image changes.
$(BOOT_FAT): $(BUILD_DIR)/kernel/kernel.elf
	@mkdir -p $(dir $@)
	rm -f $@
	dd if=/dev/zero of=$@ bs=512 count=$(BOOT_PART_SECTORS) 2>/dev/null
	mformat -F -i $@ -v BOOT ::
	mcopy -i $@ $(BUILD_DIR)/kernel/kernel.elf ::kernel.elf

# ROOT partition: a standalone axiomefs volume populated from the manifest.
# mkfs_axiomefs gets offset 0 because this file *is* the whole partition.
$(ROOT_AXFS): $(BUILD_DIR)/kernel/mkfs_axiomefs root_manifest.txt $(MANIFEST_BINS)
	@mkdir -p $(dir $@)
	rm -f $@
	dd if=/dev/zero of=$@ bs=512 count=$(ROOT_PART_SECTORS) 2>/dev/null
	$(BUILD_DIR)/kernel/mkfs_axiomefs $@ 0 root_manifest.txt

# Assemble a 256MB MBR disk:
#   * partition 0: FAT32 "BOOT" (LBA 2048, 130024 sectors) holding kernel.elf
#   * partition 1: axiomefs "ROOT" (LBA 131072) populated from root_manifest.txt
# The disk image is only zeroed/partitioned when missing or when mkpart.py
# changed; existing partitions are overlaid into it only when they changed.
$(DISK_PATH): kernel $(BUILD_DIR)/kernel/mkfs_axiomefs $(BOOT_FAT) $(ROOT_AXFS) tools/mkpart.py
	@mkdir -p $(BUILD_DIR)
	@target=$(DISK_PATH); \
	if [ ! -f "$$target" ] || [ tools/mkpart.py -nt "$$target" ]; then \
		printf '%s\n' 'disk.img: creating fresh image + MBR'; \
		dd if=/dev/zero of="$$target" bs=512 count=$(DISK_SECTORS) 2>/dev/null; \
		$(PYTHON) tools/mkpart.py "$$target"; \
		overlay_boot=1; overlay_root=1; \
	else \
		overlay_boot=0; overlay_root=0; \
	fi; \
	if [ "$$overlay_boot" -eq 1 ] || [ $(BOOT_FAT) -nt "$$target" ]; then \
		printf '%s\n' 'disk.img: overlay BOOT partition'; \
		dd if=$(BOOT_FAT) of="$$target" bs=512 seek=$(BOOT_PART_LBA) conv=notrunc 2>/dev/null; \
	fi; \
	if [ "$$overlay_root" -eq 1 ] || [ $(ROOT_AXFS) -nt "$$target" ]; then \
		printf '%s\n' 'disk.img: overlay ROOT partition'; \
		dd if=$(ROOT_AXFS) of="$$target" bs=512 seek=$(ROOT_PART_LBA) conv=notrunc 2>/dev/null; \
	fi; \
	touch "$$target"

# Convenience alias so `make disk.img` / `make run` / `make install` work.
disk.img: $(DISK_PATH)

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

run-usb: iso disk.img
	qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd \
		-cdrom $(BUILD_DIR)/axiome.iso \
		-drive file=$(BUILD_DIR)/disk.img,format=raw,if=ide,index=0,media=disk \
		-m 512M -serial stdio -display sdl \
		-device qemu-xhci,id=xhci -device usb-kbd,bus=xhci.0 \
		-device usb-mouse,bus=xhci.0

test-hid:
	@mkdir -p $(BUILD_DIR)/tests
	$(HOSTCC) -std=c11 -O2 -Wall -Wextra -Werror \
		-I$(REPO_ROOT)/kernel -o $(BUILD_DIR)/tests/hid_boot_test \
		tests/hid_boot_test.c kernel/hid_boot.c
	$(BUILD_DIR)/tests/hid_boot_test

debug: iso disk.img
	qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd \
		-cdrom $(BUILD_DIR)/axiome.iso \
		-drive file=$(BUILD_DIR)/disk.img,format=raw,if=ide,index=0,media=disk \
		-m 512M -serial stdio -s -S

install: disk.img
	sudo $(shell pwd)/tools/install.sh $(DEV) $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)/isodir $(BUILD_DIR)/boot.fat \
		$(BUILD_DIR)/root.axfs $(BUILD_DIR)/disk.img
	$(MAKE) -C kernel clean BUILD_DIR=$(BUILD_DIR)/kernel

distclean:
	rm -rf $(BUILD_DIR)
