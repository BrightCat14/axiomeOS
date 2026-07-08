REPO_ROOT := $(realpath ../..)

# Cross-compiler (same toolchain the kernel is built with).
CC := $(HOME)/opt/cross/bin/x86_64-elf-gcc
LD := $(HOME)/opt/cross/bin/x86_64-elf-ld

# Modules are freestanding, position-independent in practice via the large
# code model: every cross-module reference is a 64-bit absolute relocation,
# resolved by the kernel loader against the .ksymtab export table.
CFLAGS := -ffreestanding -nostdlib -nostartfiles -mno-red-zone -mcmodel=large \
          -mno-mmx -mno-sse -mno-sse2 -O2 -g -pipe -Wall -Wextra \
          -I$(REPO_ROOT) -I$(REPO_ROOT)/kernel

MODULES := e1000.kxt hello.kxt

.PHONY: all clean
all: $(MODULES)

# Compile to a relocatable object, then `ld -r` to keep relocations and the
# symbol table so the kernel loader can relocate + resolve at load time.
%.kxt: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $*.o $<
	$(LD) -r -o $@ $*.o
	rm -f $*.o

clean:
	rm -f $(MODULES) *.o
