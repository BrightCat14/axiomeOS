# axboot UEFI bootloader (gnu-efi BOOTX64.EFI).
# Compiled with the existing x86_64-elf cross toolchain against the system
# gnu-efi headers/libs (one-time `apt install gnu-efi`). The final PE32+
# conversion needs a PE-capable objcopy, so the host objcopy is used for
# that last step only.

REPO_ROOT := $(realpath ..)
BUILD_DIR := $(REPO_ROOT)/build/bootloader

CROSS   := $(HOME)/opt/cross/bin/x86_64-elf-
CC      := $(CROSS)gcc
LD      := $(CROSS)ld
OBJCOPY := objcopy

EFI_INC := /usr/include/efi
EFI_LIB := /usr/lib

CFLAGS  := -I$(REPO_ROOT)/include -I$(EFI_INC) -I$(EFI_INC)/x86_64
# HAVE_USE_MS_ABI is mandatory: it makes EFIAPI expand to ms_abi so every
# BootServices/protocol call uses the UEFI x86_64 calling convention.
# Without it the calls silently pass garbage (INVALID_PARAMETER, crashes).
CFLAGS  += -DHAVE_USE_MS_ABI
CFLAGS  += -fno-stack-protector -fpic -fshort-wchar -ffreestanding
CFLAGS  += -fno-strict-aliasing
# NOTE: do NOT use -ffunction-sections/-fdata-sections here. Per-variable
# .bss.* sections are orphans the gnu-efi linker script does not collect
# (*(.bss) matches only plain .bss), so they land past SizeOfImage, objcopy
# drops them, and the variables point at unowned RAM that firmware reuses
# (we debugged exactly this: g_ctx read back as 0xAF poison after
# ExitBootServices on ISO boot).
CFLAGS  += -mno-red-zone -mno-mmx -mno-sse -mno-sse2
CFLAGS  += -O2 -g -Wall -Wextra -Werror

LDFLAGS := -nostdlib -znocombreloc -z noexecstack
LDFLAGS += -T $(EFI_LIB)/elf_x86_64_efi.lds -shared -Bsymbolic
LDFLAGS += -L$(EFI_LIB)

SRCS := main.c gop.c acpi.c fs.c elf.c mmap.c boot.c
OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SRCS))

EFI_SO  := $(BUILD_DIR)/BOOTX64.so
EFI_BIN := $(BUILD_DIR)/BOOTX64.EFI

.PHONY: all clean

all: $(EFI_BIN)

$(BUILD_DIR)/%.o: %.c bootloader.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

$(EFI_SO): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $(EFI_LIB)/crt0-efi-x86_64.o $^ -lgnuefi -lefi

# Keep only the sections firmware needs; mark the image as an EFI app.
$(EFI_BIN): $(EFI_SO)
	$(OBJCOPY) -j .text -j .sdata -j .data -j .dynamic -j .dynsym \
		-j .rel -j .rela -j .rel.* -j .rela.* -j .reloc \
		--target=efi-app-x86_64 --subsystem=10 $^ $@

clean:
	rm -rf $(BUILD_DIR)
