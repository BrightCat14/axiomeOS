REPO_ROOT := $(realpath .)
BUILD_DIR := $(REPO_ROOT)/build

.PHONY: all kernel iso run debug clean distclean

all: iso

kernel:
	$(MAKE) -C kernel BUILD_DIR=$(BUILD_DIR)/kernel

iso: kernel
	mkdir -p $(BUILD_DIR)/isodir/boot/grub
	cp $(BUILD_DIR)/kernel/kernel.elf $(BUILD_DIR)/isodir/boot/
	cp grub.cfg $(BUILD_DIR)/isodir/boot/grub/
	PATH="/tmp/opencode/mtools-install/bin:$$PATH" grub-mkrescue -o $(BUILD_DIR)/axiome.iso $(BUILD_DIR)/isodir 2>/dev/null

run: iso
	qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd \
		-cdrom $(BUILD_DIR)/axiome.iso -m 512M -serial stdio -display sdl

run-fb: iso
	qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd \
		-cdrom $(BUILD_DIR)/axiome.iso -m 512M -serial stdio -vga std -display sdl

debug: iso
	qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd \
		-cdrom $(BUILD_DIR)/axiome.iso -m 512M -serial stdio -s -S

clean:
	rm -rf $(BUILD_DIR)/isodir
	$(MAKE) -C kernel clean BUILD_DIR=$(BUILD_DIR)/kernel

distclean:
	rm -rf $(BUILD_DIR)
