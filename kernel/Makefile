REPO_ROOT := $(realpath ..)
BUILD_DIR := $(REPO_ROOT)/build/kernel

CC      := $(HOME)/opt/cross/bin/x86_64-elf-gcc
CXX     := $(HOME)/opt/cross/bin/x86_64-elf-g++
AS      := $(HOME)/opt/cross/bin/x86_64-elf-as
LD      := $(HOME)/opt/cross/bin/x86_64-elf-ld

CFLAGS  = -ffreestanding -nostdlib -nostartfiles
CFLAGS += -mno-red-zone -mcmodel=kernel -mno-mmx -mno-sse -mno-sse2
CFLAGS += -O2 -g -pipe
CFLAGS += -Wall -Wextra -Wpedantic
CFLAGS += -I$(REPO_ROOT) -I$(REPO_ROOT)/kernel -I$(REPO_ROOT)/include
CFLAGS += -DGIT_COMMIT=\"$(shell git rev-parse --short HEAD)\"
CFLAGS += -DBUILD_TIME=\"$(shell date +%s)\"
CFLAGS += -DBUILD_TIME_UNIX=$(shell date +%s)ULL
CFLAGS += -DBUILD_TZ_OFFSET_SEC=$(shell python3 -c "import time; print(-time.timezone if not time.daylight else -time.altzone)")LL

# C++ is used for the hardware abstraction layer. Exceptions/RTTI and the
# standard library are unavailable in the kernel; placement new into static
# storage means no .init_array support is required.
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti -fno-threadsafe-statics
CXXFLAGS += -fno-use-cxa-atexit -fno-unwind-tables -fno-asynchronous-unwind-tables

LDFLAGS = -nostdlib -T $(REPO_ROOT)/linker.ld

C_SRCS := $(wildcard *.c) $(wildcard arch/x86_64/*.c)
# Built-in storage drivers: their sources live under modules/ but they link
# into kernel.elf (not .kxt) and register via driver_init(). The define
# switches their packaging tail from MODULE_INIT to *_driver_init().
C_SRCS += modules/sata.c modules/nvme.c
$(BUILD_DIR)/modules/sata.o $(BUILD_DIR)/modules/nvme.o: CFLAGS += -DAXIOME_BUILTIN_DRIVER
CXX_SRCS := $(wildcard hal/*.cpp) $(wildcard arch/x86_64/hal/*.cpp)
ASM_SRCS := $(wildcard arch/x86_64/*.S)
C_OBJS := $(patsubst %.c, $(BUILD_DIR)/%.o, $(C_SRCS))
CXX_OBJS := $(patsubst %.cpp, $(BUILD_DIR)/%.o, $(CXX_SRCS))
ASM_OBJS := $(patsubst %.S, $(BUILD_DIR)/%.o, $(ASM_SRCS))

# Userspace programs. They are built into the source tree as .elf and shipped
# on the root partition (root_manifest.txt), then loaded from disk at runtime
# by the ELF loader. They are deliberately NOT embedded in the kernel image.
USER_PROGS := init hello cat echo sh ls mkdir cp mv rm touch whoami uname \
	login su chmod chown vfs_test ipc_test proc_test driver_test net_test \
	kill kxtload kxtunload 	syslogd oobe date burn forkt
USER_ELFS := $(addprefix userspace/,$(addsuffix .elf,$(USER_PROGS)))
USER_OBJS := $(patsubst %,$(BUILD_DIR)/%_user.o,$(USER_PROGS))

USER_CFLAGS := -ffreestanding -nostdlib -nostartfiles -fno-builtin \
	-fno-stack-protector -static -fno-pic -fno-pie -mcmodel=large -O2 \
	-mno-red-zone -Wall -Wextra -I userspace/libc

LIBC_SRCS := userspace/libc/errno.c userspace/libc/string.c \
	userspace/libc/stdlib.c userspace/libc/stdio.c \
	userspace/libc/syscall.c userspace/libc/signal.c userspace/libc/crt0.c \
	userspace/libc/time.c
LIBC_OBJS := $(patsubst userspace/libc/%.c, $(BUILD_DIR)/libc_%.o, $(LIBC_SRCS))

.PHONY: all clean modules

all: $(BUILD_DIR)/kernel.elf modules $(USER_ELFS)

# Build loadable .kxt modules (e1000, hello, ...) into the source tree so the
# disk image builder (mkfs_axiomefs) can embed them under /System/Extensions.
modules:
	$(MAKE) -C modules

$(BUILD_DIR)/kernel.elf: $(ASM_OBJS) $(C_OBJS) $(CXX_OBJS)
	@mkdir -p $(@D)
	$(LD) $(LDFLAGS) -o $@ $^ -z max-page-size=0x1000

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(@D)
	$(AS) -c -o $@ $<

$(BUILD_DIR)/%_user.o: userspace/%.c
	@mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/libc_%.o: userspace/libc/%.c
	@mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -c $< -o $@

userspace/%.elf: $(BUILD_DIR)/%_user.o $(LIBC_OBJS)
	@mkdir -p $(@D)
	$(LD) -T userspace/link.ld -o $@ $^

clean:
	rm -rf $(BUILD_DIR)
	$(MAKE) -C modules clean
