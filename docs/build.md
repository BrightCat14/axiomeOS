# axiomeOS — Build System

> Build system: GNU Make
> Toolchain: x86_64-elf-gcc (cross-compiler in WSL)
> Bootloader: custom UEFI `BOOTX64.EFI` (gnu-efi, axboot protocol)
> Output: `build/disk.img` → QEMU (OVMF)

---

## Top-Level Makefile Targets

| Target | Description |
|--------|-------------|
| `make all` | Build kernel + bootloader + ISO |
| `make kernel` | Build `build/kernel/kernel.elf` only |
| `make bootloader` | Build `build/bootloader/BOOTX64.EFI` only |
| `make iso` | Build `build/axiome.iso` (UEFI El Torito ESP image) |
| `make disk.img` | Build `build/disk.img` (FAT32 ESP + axiomefs ROOT) |
| `make run` | Boot `disk.img` directly in QEMU (default) |
| `make run-iso` | Boot the ISO in QEMU (hardware-test path) |
| `make debug` | Boot `disk.img` in QEMU with GDB stub |
| `make clean` | Remove build artifacts (keeps `build/` dir) |
| `make distclean` | Remove entire `build/` directory |
| `make test` | Host-side unit tests (no QEMU needed) |

---

## Build Flow

```
Source files (*.c, *.S)
    │ x86_64-elf-gcc -ffreestanding -nostdlib -mno-red-zone -mcmodel=kernel -O2 -g
    ▼
Object files (*.o)
    │ x86_64-elf-ld -T linker.ld          (boot.o first → _start @ 0xFFFFFFFF80200000)
    ▼
kernel.elf ──┐
             ├─► boot.fat (FAT32 ESP: EFI/BOOT/BOOTX64.EFI + kernel.elf)
BOOTX64.EFI ─┘         │ overlay at LBA 2048 of disk.img (ESP)
                       │ copy as esp.img into the ISO (El Torito)
                       ▼
QEMU -bios OVMF (boots BOOTX64.EFI → axboot handoff → kmain(axboot_info))
```

### UEFI bootloader (`bootloader/`)

`BOOTX64.EFI` is a gnu-efi application compiled with the same
`x86_64-elf` cross toolchain (`-fshort-wchar -fpic -mno-red-zone`,
plus `-DHAVE_USE_MS_ABI` so EFI calls use the UEFI calling convention),
linked against the system gnu-efi (`crt0-efi-x86_64.o`, `libgnuefi.a`,
`elf_x86_64_efi.lds`); only the final PE32+ conversion uses the host
`objcopy`. It reads `\kernel.elf` from the ESP via SimpleFileSystem,
places PT_LOAD segments at `p_paddr`, builds the `axboot_info` handoff
(`include/axboot.h`), exits boot services and jumps to the native
64-bit kernel entry with `RDI` = bootinfo physical address.

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

The kernel is linked at `KERNEL_VIRT_BASE = 0xFFFFFFFF80000000`, but loaded by the axboot loader at `KERNEL_PHYS_BASE = 0x200000`.

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
        *(.text*)
        *(.gnu.linkonce.t*)
    }
    ...
}
```

The `AT(...)` directive tells the linker the physical load address while the symbol addresses are virtual. The loader places ELF segments using the `p_paddr` field. (`boot.o` links first so `_start` lands at `0xFFFFFFFF80200000`, the address the loader jumps to.)

---

## Boot Handoff (`include/axboot.h`)

There is no GRUB and no multiboot2. The kernel entry is native 64-bit:
`boot.S` (`kernel/arch/x86_64/boot.S`) receives `RDI` = physical address
of `struct axboot_info` (magic `AXIOME_BOOT_MAGIC`, version 1), carrying
the memory map, the GOP framebuffer, the ACPI RSDP and the kernel
geometry. `kernel/axboot.c` validates it and publishes it to `pmm`,
`fb_init` and `hal_bootinfo`. There is no bootloader config file.

---

## QEMU Commands

### Standard run (disk image, axboot loader)

```bash
qemu-system-x86_64 \
    -bios /usr/share/ovmf/OVMF.fd \
    -drive file=build/disk.img,format=raw,if=ide,index=0,media=disk \
    -m 512M \
    -serial stdio \
    -vga std
```

### ISO run (hardware-test path)

```bash
qemu-system-x86_64 \
    -bios /usr/share/ovmf/OVMF.fd \
    -cdrom build/axiome.iso \
    -drive file=build/disk.img,format=raw,if=ide,index=0,media=disk \
    -m 512M \
    -serial stdio \
    -vga std
```

### Debug run (GDB stub)

```bash
qemu-system-x86_64 \
    -bios /usr/share/ovmf/OVMF.fd \
    -drive file=build/disk.img,format=raw,if=ide,index=0,media=disk \
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
# The BOOT partition image is itself a FAT32 ESP holding
#   EFI/BOOT/BOOTX64.EFI and kernel.elf at the volume root.
# Reuse it verbatim as the El Torito boot image:
cp build/boot.fat build/isowork/esp.img

# Generate ISO (UEFI-only, no BIOS boot catalog)
xorriso -as mkisofs -R -f -e esp.img -no-emul-boot \
    -o build/axiome.iso build/isowork
```

---

## Makefile Structure

```makefile
# Top-level Makefile

REPO_ROOT := $(realpath .)
BUILD_DIR := $(REPO_ROOT)/build

.PHONY: all kernel bootloader iso run run-iso debug clean distclean

all: iso

kernel:
    $(MAKE) -C kernel BUILD_DIR=$(BUILD_DIR)/kernel

bootloader:
    $(MAKE) -C bootloader

iso: kernel bootloader $(BOOT_FAT)
    rm -rf $(BUILD_DIR)/isowork
    mkdir -p $(BUILD_DIR)/isowork
    cp $(BOOT_FAT) $(BUILD_DIR)/isowork/esp.img
    xorriso -as mkisofs -R -f -e esp.img -no-emul-boot \
        -o $(BUILD_DIR)/axiome.iso $(BUILD_DIR)/isowork

run: disk.img
    qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd \
        -drive file=$(BUILD_DIR)/disk.img,format=raw,if=ide -m 512M -serial stdio

debug: disk.img
    qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd \
        -drive file=$(BUILD_DIR)/disk.img,format=raw,if=ide -m 512M -serial stdio -s -S

clean:
    rm -rf $(BUILD_DIR)/isowork $(BUILD_DIR)/boot.fat ...
    $(MAKE) -C kernel clean
    $(MAKE) -C bootloader clean

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
