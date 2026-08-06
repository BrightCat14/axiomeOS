REPO_ROOT := $(realpath ..)
BUILD_DIR := $(REPO_ROOT)/build/kernel

CC      := $(HOME)/opt/cross/bin/x86_64-elf-gcc
AS      := $(HOME)/opt/cross/bin/x86_64-elf-as
LD      := $(HOME)/opt/cross/bin/x86_64-elf-ld

CFLAGS  = -ffreestanding -nostdlib -nostartfiles
CFLAGS += -mno-red-zone -mcmodel=kernel -mno-mmx -mno-sse -mno-sse2
CFLAGS += -O2 -g -pipe
CFLAGS += -Wall -Wextra -Wpedantic
CFLAGS += -I$(REPO_ROOT)
CFLAGS += -DGIT_COMMIT=\"$(shell git rev-parse --short HEAD)\"
CFLAGS += -DBUILD_TIME=\"$(shell date +%s)\"

LDFLAGS = -nostdlib -T $(REPO_ROOT)/linker.ld

C_SRCS := $(wildcard *.c) $(wildcard arch/x86_64/*.c)
ASM_SRCS := $(wildcard arch/x86_64/*.S)
C_OBJS := $(patsubst %.c, $(BUILD_DIR)/%.o, $(C_SRCS))
ASM_OBJS := $(patsubst %.S, $(BUILD_DIR)/%.o, $(ASM_SRCS))

USER_ELF := userspace/init.elf
USER_ELF_OBJ := $(BUILD_DIR)/init_elf.o
C_OBJS += $(USER_ELF_OBJ)

USER_HELLO_ELF := userspace/hello.elf
USER_HELLO_ELF_OBJ := $(BUILD_DIR)/hello_elf.o
C_OBJS += $(USER_HELLO_ELF_OBJ)

USER_CAT_ELF := userspace/cat.elf
USER_CAT_ELF_OBJ := $(BUILD_DIR)/cat_elf.o
C_OBJS += $(USER_CAT_ELF_OBJ)

USER_ECHO_ELF := userspace/echo.elf
USER_ECHO_ELF_OBJ := $(BUILD_DIR)/echo_elf.o
C_OBJS += $(USER_ECHO_ELF_OBJ)

USER_SH_ELF := userspace/sh.elf
USER_SH_ELF_OBJ := $(BUILD_DIR)/sh_elf.o
C_OBJS += $(USER_SH_ELF_OBJ)

USER_LS_ELF := userspace/ls.elf
USER_LS_ELF_OBJ := $(BUILD_DIR)/ls_elf.o
C_OBJS += $(USER_LS_ELF_OBJ)

USER_MKDIR_ELF := userspace/mkdir.elf
USER_MKDIR_ELF_OBJ := $(BUILD_DIR)/mkdir_elf.o
C_OBJS += $(USER_MKDIR_ELF_OBJ)

USER_CP_ELF := userspace/cp.elf
USER_CP_ELF_OBJ := $(BUILD_DIR)/cp_elf.o
C_OBJS += $(USER_CP_ELF_OBJ)

USER_MV_ELF := userspace/mv.elf
USER_MV_ELF_OBJ := $(BUILD_DIR)/mv_elf.o
C_OBJS += $(USER_MV_ELF_OBJ)

USER_RM_ELF := userspace/rm.elf
USER_RM_ELF_OBJ := $(BUILD_DIR)/rm_elf.o
C_OBJS += $(USER_RM_ELF_OBJ)

USER_TOUCH_ELF := userspace/touch.elf
USER_TOUCH_ELF_OBJ := $(BUILD_DIR)/touch_elf.o
C_OBJS += $(USER_TOUCH_ELF_OBJ)

USER_WHOAMI_ELF := userspace/whoami.elf
USER_WHOAMI_ELF_OBJ := $(BUILD_DIR)/whoami_elf.o
C_OBJS += $(USER_WHOAMI_ELF_OBJ)

USER_UNAME_ELF := userspace/uname.elf
USER_UNAME_ELF_OBJ := $(BUILD_DIR)/uname_elf.o
C_OBJS += $(USER_UNAME_ELF_OBJ)

USER_LOGIN_ELF := userspace/login.elf
USER_LOGIN_ELF_OBJ := $(BUILD_DIR)/login_elf.o
C_OBJS += $(USER_LOGIN_ELF_OBJ)

USER_SU_ELF := userspace/su.elf
USER_SU_ELF_OBJ := $(BUILD_DIR)/su_elf.o
C_OBJS += $(USER_SU_ELF_OBJ)

USER_CHMOD_ELF := userspace/chmod.elf
USER_CHMOD_ELF_OBJ := $(BUILD_DIR)/chmod_elf.o
C_OBJS += $(USER_CHMOD_ELF_OBJ)

USER_CHOWN_ELF := userspace/chown.elf
USER_CHOWN_ELF_OBJ := $(BUILD_DIR)/chown_elf.o
C_OBJS += $(USER_CHOWN_ELF_OBJ)

USER_VFS_TEST_ELF := userspace/vfs_test.elf
USER_VFS_TEST_ELF_OBJ := $(BUILD_DIR)/vfs_test_elf.o
C_OBJS += $(USER_VFS_TEST_ELF_OBJ)

USER_IPC_TEST_ELF := userspace/ipc_test.elf
USER_IPC_TEST_ELF_OBJ := $(BUILD_DIR)/ipc_test_elf.o
C_OBJS += $(USER_IPC_TEST_ELF_OBJ)

USER_PROC_TEST_ELF := userspace/proc_test.elf
USER_PROC_TEST_ELF_OBJ := $(BUILD_DIR)/proc_test_elf.o
C_OBJS += $(USER_PROC_TEST_ELF_OBJ)

USER_DRIVER_TEST_ELF := userspace/driver_test.elf
USER_DRIVER_TEST_ELF_OBJ := $(BUILD_DIR)/driver_test_elf.o
C_OBJS += $(USER_DRIVER_TEST_ELF_OBJ)

USER_NET_TEST_ELF := userspace/net_test.elf
USER_NET_TEST_ELF_OBJ := $(BUILD_DIR)/net_test_elf.o
C_OBJS += $(USER_NET_TEST_ELF_OBJ)

USER_KILL_ELF := userspace/kill.elf
USER_KILL_ELF_OBJ := $(BUILD_DIR)/kill_elf.o
C_OBJS += $(USER_KILL_ELF_OBJ)

USER_KXTLOAD_ELF := userspace/kxtload.elf
USER_KXTLOAD_ELF_OBJ := $(BUILD_DIR)/kxtload_elf.o
C_OBJS += $(USER_KXTLOAD_ELF_OBJ)

USER_KXTUNLOAD_ELF := userspace/kxtunload.elf
USER_KXTUNLOAD_ELF_OBJ := $(BUILD_DIR)/kxtunload_elf.o
C_OBJS += $(USER_KXTUNLOAD_ELF_OBJ)

USER_SYSLOGD_ELF := userspace/syslogd.elf
USER_SYSLOGD_ELF_OBJ := $(BUILD_DIR)/syslogd_elf.o
C_OBJS += $(USER_SYSLOGD_ELF_OBJ)

USER_CFLAGS := -ffreestanding -nostdlib -nostartfiles -fno-builtin \
	-fno-stack-protector -static -fno-pic -fno-pie -mcmodel=large -O2 \
	-mno-red-zone -Wall -Wextra -I userspace/libc

LIBC_SRCS := userspace/libc/errno.c userspace/libc/string.c \
	userspace/libc/stdlib.c userspace/libc/stdio.c \
	userspace/libc/syscall.c userspace/libc/signal.c userspace/libc/crt0.c
LIBC_OBJS := $(patsubst userspace/libc/%.c, $(BUILD_DIR)/libc_%.o, $(LIBC_SRCS))

.PHONY: all clean modules

all: $(BUILD_DIR)/kernel.elf modules

# Build loadable .kxt modules (e1000, hello, ...) into the source tree so the
# disk image builder (mkfs_axiomefs) can embed them under /System/Extensions.
modules:
	$(MAKE) -C modules

$(BUILD_DIR)/kernel.elf: $(C_OBJS) $(ASM_OBJS)
	@mkdir -p $(@D)
	$(LD) $(LDFLAGS) -o $@ $^ -z max-page-size=0x1000

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(@D)
	$(AS) -c -o $@ $<

$(USER_ELF): userspace/init.c userspace/link.ld $(LIBC_OBJS)
	@mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -c $< -o $(BUILD_DIR)/init_user.o
	$(LD) -T userspace/link.ld -o $@ $(BUILD_DIR)/init_user.o $(LIBC_OBJS)

$(USER_ELF_OBJ): $(USER_ELF)
	$(LD) -r -b binary -o $@ $<

$(USER_HELLO_ELF): userspace/hello.c userspace/link.ld $(LIBC_OBJS)
	@mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -c $< -o $(BUILD_DIR)/hello_user.o
	$(LD) -T userspace/link.ld -o $@ $(BUILD_DIR)/hello_user.o $(LIBC_OBJS)

$(USER_CAT_ELF): userspace/cat.c userspace/link.ld $(LIBC_OBJS)
	@mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -c $< -o $(BUILD_DIR)/cat_user.o
	$(LD) -T userspace/link.ld -o $@ $(BUILD_DIR)/cat_user.o $(LIBC_OBJS)

$(USER_ECHO_ELF): userspace/echo.c userspace/link.ld $(LIBC_OBJS)
	@mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -c $< -o $(BUILD_DIR)/echo_user.o
	$(LD) -T userspace/link.ld -o $@ $(BUILD_DIR)/echo_user.o $(LIBC_OBJS)

$(USER_SH_ELF): userspace/sh.c userspace/link.ld $(LIBC_OBJS)
	@mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -c $< -o $(BUILD_DIR)/sh_user.o
	$(LD) -T userspace/link.ld -o $@ $(BUILD_DIR)/sh_user.o $(LIBC_OBJS)

$(BUILD_DIR)/libc_%.o: userspace/libc/%.c
	@mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_HELLO_ELF_OBJ): $(USER_HELLO_ELF)
	$(LD) -r -b binary -o $@ $<

$(USER_CAT_ELF_OBJ): $(USER_CAT_ELF)
	$(LD) -r -b binary -o $@ $<

$(USER_ECHO_ELF_OBJ): $(USER_ECHO_ELF)
	$(LD) -r -b binary -o $@ $<

$(USER_SH_ELF_OBJ): $(USER_SH_ELF)
	$(LD) -r -b binary -o $@ $<

$(USER_LS_ELF): userspace/ls.c userspace/link.ld $(LIBC_OBJS)
	@mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -c $< -o $(BUILD_DIR)/ls_user.o
	$(LD) -T userspace/link.ld -o $@ $(BUILD_DIR)/ls_user.o $(LIBC_OBJS)

$(USER_MKDIR_ELF): userspace/mkdir.c userspace/link.ld $(LIBC_OBJS)
	@mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -c $< -o $(BUILD_DIR)/mkdir_user.o
	$(LD) -T userspace/link.ld -o $@ $(BUILD_DIR)/mkdir_user.o $(LIBC_OBJS)

$(USER_CP_ELF): userspace/cp.c userspace/link.ld $(LIBC_OBJS)
	@mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -c $< -o $(BUILD_DIR)/cp_user.o
	$(LD) -T userspace/link.ld -o $@ $(BUILD_DIR)/cp_user.o $(LIBC_OBJS)

$(USER_MV_ELF): userspace/mv.c userspace/link.ld $(LIBC_OBJS)
	@mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -c $< -o $(BUILD_DIR)/mv_user.o
	$(LD) -T userspace/link.ld -o $@ $(BUILD_DIR)/mv_user.o $(LIBC_OBJS)

$(USER_RM_ELF): userspace/rm.c userspace/link.ld $(LIBC_OBJS)
	@mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -c $< -o $(BUILD_DIR)/rm_user.o
	$(LD) -T userspace/link.ld -o $@ $(BUILD_DIR)/rm_user.o $(LIBC_OBJS)

$(USER_TOUCH_ELF): userspace/touch.c userspace/link.ld $(LIBC_OBJS)
	@mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -c $< -o $(BUILD_DIR)/touch_user.o
	$(LD) -T userspace/link.ld -o $@ $(BUILD_DIR)/touch_user.o $(LIBC_OBJS)

$(USER_WHOAMI_ELF): userspace/whoami.c userspace/link.ld $(LIBC_OBJS)
	@mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -c $< -o $(BUILD_DIR)/whoami_user.o
	$(LD) -T userspace/link.ld -o $@ $(BUILD_DIR)/whoami_user.o $(LIBC_OBJS)

$(USER_UNAME_ELF): userspace/uname.c userspace/link.ld $(LIBC_OBJS)
	@mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -c $< -o $(BUILD_DIR)/uname_user.o
	$(LD) -T userspace/link.ld -o $@ $(BUILD_DIR)/uname_user.o $(LIBC_OBJS)

$(USER_LOGIN_ELF): userspace/login.c userspace/link.ld $(LIBC_OBJS)
	@mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -c $< -o $(BUILD_DIR)/login_user.o
	$(LD) -T userspace/link.ld -o $@ $(BUILD_DIR)/login_user.o $(LIBC_OBJS)

$(USER_SU_ELF): userspace/su.c userspace/link.ld $(LIBC_OBJS)
	@mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -c $< -o $(BUILD_DIR)/su_user.o
	$(LD) -T userspace/link.ld -o $@ $(BUILD_DIR)/su_user.o $(LIBC_OBJS)

$(USER_CHMOD_ELF): userspace/chmod.c userspace/link.ld $(LIBC_OBJS)
	@mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -c $< -o $(BUILD_DIR)/chmod_user.o
	$(LD) -T userspace/link.ld -o $@ $(BUILD_DIR)/chmod_user.o $(LIBC_OBJS)

$(USER_CHOWN_ELF): userspace/chown.c userspace/link.ld $(LIBC_OBJS)
	@mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -c $< -o $(BUILD_DIR)/chown_user.o
	$(LD) -T userspace/link.ld -o $@ $(BUILD_DIR)/chown_user.o $(LIBC_OBJS)

$(USER_VFS_TEST_ELF): userspace/vfs_test.c userspace/link.ld $(LIBC_OBJS)
	@mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -c $< -o $(BUILD_DIR)/vfs_test_user.o
	$(LD) -T userspace/link.ld -o $@ $(BUILD_DIR)/vfs_test_user.o $(LIBC_OBJS)

$(USER_IPC_TEST_ELF): userspace/ipc_test.c userspace/link.ld $(LIBC_OBJS)
	@mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -c $< -o $(BUILD_DIR)/ipc_test_user.o
	$(LD) -T userspace/link.ld -o $@ $(BUILD_DIR)/ipc_test_user.o $(LIBC_OBJS)

$(USER_PROC_TEST_ELF): userspace/proc_test.c userspace/link.ld $(LIBC_OBJS)
	@mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -c $< -o $(BUILD_DIR)/proc_test_user.o
	$(LD) -T userspace/link.ld -o $@ $(BUILD_DIR)/proc_test_user.o $(LIBC_OBJS)

$(USER_DRIVER_TEST_ELF): userspace/driver_test.c userspace/link.ld $(LIBC_OBJS)
	@mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -c $< -o $(BUILD_DIR)/driver_test_user.o
	$(LD) -T userspace/link.ld -o $@ $(BUILD_DIR)/driver_test_user.o $(LIBC_OBJS)

$(USER_NET_TEST_ELF): userspace/net_test.c userspace/link.ld $(LIBC_OBJS)
	@mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -c $< -o $(BUILD_DIR)/net_test_user.o
	$(LD) -T userspace/link.ld -o $@ $(BUILD_DIR)/net_test_user.o $(LIBC_OBJS)

$(USER_LS_ELF_OBJ): $(USER_LS_ELF)
	$(LD) -r -b binary -o $@ $<

$(USER_MKDIR_ELF_OBJ): $(USER_MKDIR_ELF)
	$(LD) -r -b binary -o $@ $<

$(USER_CP_ELF_OBJ): $(USER_CP_ELF)
	$(LD) -r -b binary -o $@ $<

$(USER_MV_ELF_OBJ): $(USER_MV_ELF)
	$(LD) -r -b binary -o $@ $<

$(USER_RM_ELF_OBJ): $(USER_RM_ELF)
	$(LD) -r -b binary -o $@ $<

$(USER_TOUCH_ELF_OBJ): $(USER_TOUCH_ELF)
	$(LD) -r -b binary -o $@ $<

$(USER_WHOAMI_ELF_OBJ): $(USER_WHOAMI_ELF)
	$(LD) -r -b binary -o $@ $<

$(USER_UNAME_ELF_OBJ): $(USER_UNAME_ELF)
	$(LD) -r -b binary -o $@ $<

$(USER_LOGIN_ELF_OBJ): $(USER_LOGIN_ELF)
	$(LD) -r -b binary -o $@ $<

$(USER_SU_ELF_OBJ): $(USER_SU_ELF)
	$(LD) -r -b binary -o $@ $<

$(USER_CHMOD_ELF_OBJ): $(USER_CHMOD_ELF)
	$(LD) -r -b binary -o $@ $<

$(USER_CHOWN_ELF_OBJ): $(USER_CHOWN_ELF)
	$(LD) -r -b binary -o $@ $<

$(USER_VFS_TEST_ELF_OBJ): $(USER_VFS_TEST_ELF)
	$(LD) -r -b binary -o $@ $<

$(USER_IPC_TEST_ELF_OBJ): $(USER_IPC_TEST_ELF)
	$(LD) -r -b binary -o $@ $<

$(USER_PROC_TEST_ELF_OBJ): $(USER_PROC_TEST_ELF)
	$(LD) -r -b binary -o $@ $<

$(USER_DRIVER_TEST_ELF_OBJ): $(USER_DRIVER_TEST_ELF)
	$(LD) -r -b binary -o $@ $<
$(USER_NET_TEST_ELF_OBJ): $(USER_NET_TEST_ELF)
	$(LD) -r -b binary -o $@ $<

$(USER_KILL_ELF): userspace/kill.c userspace/link.ld $(LIBC_OBJS)
	@mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -c $< -o $(BUILD_DIR)/kill_user.o
	$(LD) -T userspace/link.ld -o $@ $(BUILD_DIR)/kill_user.o $(LIBC_OBJS)

$(USER_KILL_ELF_OBJ): $(USER_KILL_ELF)
	$(LD) -r -b binary -o $@ $<

$(USER_KXTLOAD_ELF): userspace/kxtload.c userspace/link.ld $(LIBC_OBJS)
	@mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -c $< -o $(BUILD_DIR)/kxtload_user.o
	$(LD) -T userspace/link.ld -o $@ $(BUILD_DIR)/kxtload_user.o $(LIBC_OBJS)

$(USER_KXTLOAD_ELF_OBJ): $(USER_KXTLOAD_ELF)
	$(LD) -r -b binary -o $@ $<

$(USER_KXTUNLOAD_ELF): userspace/kxtunload.c userspace/link.ld $(LIBC_OBJS)
	@mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -c $< -o $(BUILD_DIR)/kxtunload_user.o
	$(LD) -T userspace/link.ld -o $@ $(BUILD_DIR)/kxtunload_user.o $(LIBC_OBJS)

$(USER_KXTUNLOAD_ELF_OBJ): $(USER_KXTUNLOAD_ELF)
	$(LD) -r -b binary -o $@ $<

$(USER_SYSLOGD_ELF): userspace/syslogd.c userspace/link.ld $(LIBC_OBJS)
	@mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -c $< -o $(BUILD_DIR)/syslogd_user.o
	$(LD) -T userspace/link.ld -o $@ $(BUILD_DIR)/syslogd_user.o $(LIBC_OBJS)

$(USER_SYSLOGD_ELF_OBJ): $(USER_SYSLOGD_ELF)
	$(LD) -r -b binary -o $@ $<

clean:
	rm -rf $(BUILD_DIR)
	$(MAKE) -C modules clean
