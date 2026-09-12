# Host-side tools for building axiomeOS disk images.
#   * mkfs_axiomefs  - C formatter for the axiomefs root partition
#   * mkfat.py       - minimal FAT12 boot image
#   * mkpart.py      - MBR partition table writer
#   * install.sh     - writes a complete image to a real block device

PYTHON  ?= python3
HOSTCC  ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11

# Directory that contains kernel/axiomefs.h (shared on-disk structs).
# Override with: make KERNEL_DIR=/path/to/axiomeOS
KERNEL_DIR ?= $(realpath ../axiomeOS)

BUILD_DIR ?= build
MKFS      := $(BUILD_DIR)/mkfs_axiomefs

.PHONY: all mkfs mkfat mkpart clean

all: mkfs

$(MKFS): mkfs_axiomefs.c
	@mkdir -p $(BUILD_DIR)
	$(HOSTCC) $(CFLAGS) -I$(KERNEL_DIR) -o $@ $<

mkfs: $(MKFS)

mkfat: mkfat.py
	$(PYTHON) $< $(OUT) $(EFI_DIR)

mkpart: mkpart.py
	$(PYTHON) $< $(DISK_IMG)

clean:
	rm -rf $(BUILD_DIR)
