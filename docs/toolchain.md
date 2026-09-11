# Toolchain Setup

This guide describes how to prepare a development environment for axiomeOS.

The recommended host platform is Linux. WSL2 (Ubuntu) is also supported.

---

# Requirements

axiomeOS requires:

- GCC cross-compiler targeting `x86_64-elf`
- Binutils for `x86_64-elf`
- QEMU
- OVMF (UEFI firmware)
- gnu-efi (UEFI bootloader library)
- mtools
- xorriso
- Python 3
- GNU Make

---

# Installing Dependencies

## Debian / Ubuntu

```bash
sudo apt update

sudo apt install -y \
    build-essential \
    bison \
    flex \
    libgmp3-dev \
    libmpc-dev \
    libmpfr-dev \
    texinfo \
    nasm \
    python3 \
    qemu-system-x86 \
    ovmf \
    gnu-efi \
    xorriso \
    mtools \
    gdb
```

---

# Building the Cross Compiler

axiomeOS is built using a freestanding `x86_64-elf` toolchain.

Using your host compiler is **not supported**.

Create a cross compiler using GCC and Binutils following the OSDev Wiki:

https://wiki.osdev.org/GCC_Cross-Compiler

or use the provided helper script:

```bash
bash toolchain/build-cross-compiler.sh
```

After installation, make sure the compiler is in your `PATH`:

```bash
export PATH="$HOME/opt/cross/bin:$PATH"
```

Verify:

```bash
x86_64-elf-gcc --version
x86_64-elf-ld --version
x86_64-elf-as --version
```

---

# QEMU

Verify that OVMF is installed:

```bash
ls /usr/share/ovmf/OVMF.fd
```

If the file exists, the emulator is ready.

---

# UEFI bootloader (axboot)

The `bootloader/` directory builds `BOOTX64.EFI` with the same
`x86_64-elf` cross toolchain, linked against the system gnu-efi
(`crt0-efi-x86_64.o`, `libgnuefi.a`, `elf_x86_64_efi.lds`). Only the final
PE32+ conversion uses the host `objcopy` (the cross objcopy has no PE
target). No other host dependencies are involved.

---

# WSL Notes

WSL2 is supported.

Typical workflow:

1. Edit code from Windows or WSL.
2. Build inside WSL.
3. Run using `make run`.

The repository may live anywhere accessible from WSL, for example:

```
/mnt/c/Users/<username>/Documents/axiomeOS
```
