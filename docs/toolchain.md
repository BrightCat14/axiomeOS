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
- GRUB utilities
- mtools
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
    grub-common \
    grub-pc-bin \
    grub-efi-amd64-bin \
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
