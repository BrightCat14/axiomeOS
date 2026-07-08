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
	sudo ip tuntap add dev tap0 mode tap user $(USER) || true
	sudo ip addr add 10.0.2.1/24 dev tap0 || true
	sudo ip link set tap0 up || true
	qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd \
		-cdrom $(BUILD_DIR)/axiome.iso -m 512M -serial stdio -display sdl \
		-netdev tap,id=net0,ifname=tap0,script=no,downscript=no -device e1000,netdev=net0

run-fb: iso
	sudo ip tuntap add dev tap0 mode tap user $(USER) || true
	sudo ip addr add 10.0.2.1/24 dev tap0 || true
	sudo ip link set tap0 up || true
	qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd \
		-cdrom $(BUILD_DIR)/axiome.iso -m 512M -serial stdio -vga std -display sdl \
		-netdev tap,id=net0,ifname=tap0,script=no,downscript=no -device e1000,netdev=net0

debug: iso
	sudo ip tuntap add dev tap0 mode tap user $(USER) || true
	sudo ip addr add 10.0.2.1/24 dev tap0 || true
	sudo ip link set tap0 up || true
	qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd \
		-cdrom $(BUILD_DIR)/axiome.iso -m 512M -serial stdio -s -S \
		-netdev tap,id=net0,ifname=tap0,script=no,downscript=no -device e1000,netdev=net0

clean:
	sudo ip link set tap0 down 2>/dev/null || true
	sudo ip tuntap del dev tap0 mode tap 2>/dev/null || true
	rm -rf $(BUILD_DIR)/isodir
	$(MAKE) -C kernel clean BUILD_DIR=$(BUILD_DIR)/kernel

distclean:
	rm -rf $(BUILD_DIR)
