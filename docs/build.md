# axiomeOS — Build System

> Build system: GNU Make
> Toolchain: x86_64-elf-gcc (cross-compiler in WSL)
> Output: `build/kernel.elf` → ISO image → QEMU

---

## Top-Level Makefile Targets

| Target | Description |
|--------|-------------|
| `make all` | Build kernel + ISO |
| `make kernel` | Build `build/kernel.elf` only |
| `make iso` | Build `build/axiome.iso` (kernel + grub.cfg) |
| `make run` | Build ISO + launch QEMU |
| `make debug` | Build ISO + launch QEMU with GDB stub |
| `make clean` | Remove build artifacts (keeps `build/` dir) |
| `make distclean` | Remove entire `build/` directory |
| `make test` | Build ISO + QEMU with serial output test harness |

---

## Build Flow

```
Source files (*.c, *.S)
    │ x86_64-elf-gcc -ffreestanding -nostdlib -mno-red-zone -mcmodel=kernel -O2 -g
    ▼
Object files (*.o)
    │ x86_64-elf-ld -T linker.ld
    ▼
kernel.elf
    │ copy to build/isodir/boot/kernel.elf
    │ grub-mkrescue -o build/axiome.iso build/isodir
    ▼
axiome.iso
    │ qemu-system-x86_64 -cdrom build/axiome.iso
    ▼
QEMU (boots GRUB → loads kernel → kmain)
```

---

## Compiler Flags

### Kernel C Flags

```makefile
CFLAGS  = -ffreestanding -nostdlib -nostartfiles
CFLAGS += -mno-red-zone -mcmodel=kernel -mno-mmx -mno-sse -mno-sse2
CFLAGS += -O2 -g -pipe
CFLAGS += -Wall -Wextra -Werror -Wpedantic
CFLAGS += -I$(REPO_ROOT)
```

Why:
- `-ffreestanding`: no hosted libc assumed
- `-nostdlib`: linker won't search system libs
- `-mno-red-zone`: syscalls/interrupts don't clobber red zone
- `-mcmodel=kernel`: code is linked at higher-half address (`0xFFFFFFFF80000000+`), but starts physically at lower address. The `kernel` code model means the compiler assumes code may not be reachable via a 32-bit signed offset from the program counter.
- `-mno-mmx -mno-sse -mno-sse2`: no SIMD in kernel (context switches would need to save/restore SIMD state)

### Kernel Assembler Flags

```makefile
ASFLAGS = -f elf64
```

### Linker Flags

```makefile
LDFLAGS = -nostdlib -nostartfiles -T linker.ld
```

---

## Linker Script (`linker.ld`)

The kernel is linked at `KERNEL_VIRT_BASE = 0xFFFFFFFF80000000`, but loaded by GRUB at `KERNEL_PHYS_BASE = 0x200000`.

```ld
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

KERNEL_VIRT_BASE = 0xFFFFFFFF80000000;

SECTIONS
{
    . = 0x200000;                     /* Physical load address */
    . += KERNEL_VIRT_BASE;            /* Switch to higher-half */

    .text : AT(ADDR(.text) - KERNEL_VIRT_BASE)
    {
        *(.multiboot)                 /* Multiboot2 header first */
        *(.text*)
        *(.gnu.linkonce.t*)
    }

    .rodata : AT(ADDR(.rodata) - KERNEL_VIRT_BASE)
    {
        *(.rodata*)
    }

    .data : AT(ADDR(.data) - KERNEL_VIRT_BASE)
    {
        *(.data*)
    }

    .bss : AT(ADDR(.bss) - KERNEL_VIRT_BASE)
    {
        *(COMMON)
        *(.bss*)
    }

    /DISCARD/ : { *(.comment) *(.eh_frame) *(.note*) }
}
```

The `AT(...)` directive tells the linker the physical load address while the symbol addresses are virtual. GRUB loads ELF segments using the `p_paddr` field.

---

## GRUB Configuration (`grub.cfg`)

```cfg
set timeout=3
set default=0

menuentry "axiomeOS" {
    multiboot2 /boot/kernel.elf
    boot
}

menuentry "axiomeOS (verbose)" {
    multiboot2 /boot/kernel.elf debug=1
    boot
}
```

For QEMU testing, GRUB is embedded in the ISO via `grub-mkrescue`.

---

## QEMU Commands

### Standard run

```bash
qemu-system-x86_64 \
    -bios /usr/share/ovmf/OVMF.fd \
    -cdrom build/axiome.iso \
    -m 512M \
    -serial stdio \
    -vga std
```

### Debug run (GDB stub)

```bash
qemu-system-x86_64 \
    -bios /usr/share/ovmf/OVMF.fd \
    -cdrom build/axiome.iso \
    -m 512M \
    -serial stdio \
    -s -S              # -s: -gdb tcp::1234, -S: freeze at startup
```

Then in another terminal:

```bash
x86_64-elf-gdb build/kernel.elf
(gdb) target remote :1234
(gdb) continue
```

### With KVM acceleration

```bash
qemu-system-x86_64 -enable-kvm ...
```

Only on bare-metal Linux (not inside WSL 1, but WSL 2 can sometimes use nested virtualization).

---

## ISO Generation

```bash
# Create staging directory
mkdir -p build/isodir/boot/grub

# Copy kernel
cp build/kernel.elf build/isodir/boot/kernel.elf

# Copy GRUB config
cp grub.cfg build/isodir/boot/grub/grub.cfg

# Generate ISO
grub-mkrescue -o build/axiome.iso build/isodir
```

---

## Makefile Structure

```makefile
# Top-level Makefile

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
    grub-mkrescue -o $(BUILD_DIR)/axiome.iso $(BUILD_DIR)/isodir

run: iso
    qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd \
        -cdrom $(BUILD_DIR)/axiome.iso -m 512M -serial stdio

debug: iso
    qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd \
        -cdrom $(BUILD_DIR)/axiome.iso -m 512M -serial stdio -s -S

clean:
    rm -rf $(BUILD_DIR)/isodir
    $(MAKE) -C kernel clean

distclean:
    rm -rf $(BUILD_DIR)
```

---

## Build System Evolution

| Phase | Build complexity |
|-------|-----------------|
| 0–1 | Single `Makefile`, kernel only, no libc |
| 2–6 | Kernel Makefile with arch subdirectories |
| 7–8 | Add libc build, userland build |
| 9+ | Multi-directory build with automatic dependency tracking |

The Makefiles will grow organically. No cmake/autotools — keep it simple.
