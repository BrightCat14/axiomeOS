# axiomeOS — Toolchain Setup (WSL)

> Build environment: WSL 2 (Ubuntu)
> Target: x86_64-elf (cross-compiler)
> Emulator: QEMU + OVMF (UEFI firmware for GRUB)

---

## 1. Install WSL 2

Open PowerShell as Administrator:

```powershell
wsl --install -d Ubuntu
wsl --set-default-version 2
```

Restart and set up Ubuntu user. Then update:

```bash
sudo apt update && sudo apt upgrade -y
```

---

## 2. Install Build Dependencies

```bash
# Build tools
sudo apt install -y build-essential bison flex libgmp3-dev libmpc-dev \
    libmpfr-dev texinfo nasm

# QEMU + firmware
sudo apt install -y qemu-system-x86 ovmf

# ISO creation tools
sudo apt install -y xorriso grub-pc-bin grub-efi-amd64-bin grub-common
```

**Note**: `grub-pc-bin` provides BIOS-mode GRUB modules; `grub-efi-amd64-bin` provides UEFI-mode modules. Both let `grub-mkrescue` create a hybrid ISO that boots on BIOS or UEFI.

---

## 3. Build Cross-Compiler (`x86_64-elf-gcc`)

We build a cross-compiler that targets `x86_64-elf` (no host OS dependencies). This prevents the compiler from using the host's headers/libraries.

### 3.1 Set up environment

```bash
export PREFIX="$HOME/opt/cross"
export TARGET="x86_64-elf"
export PATH="$PREFIX/bin:$PATH"
```

Add these to `~/.bashrc`:

```bash
echo 'export PATH="$HOME/opt/cross/bin:$PATH"' >> ~/.bashrc
```

### 3.2 Download sources

```bash
cd ~/src   # or wherever you keep source tarballs

# Binutils
wget https://ftp.gnu.org/gnu/binutils/binutils-2.43.tar.xz
tar -xf binutils-2.43.tar.xz

# GCC
wget https://ftp.gnu.org/gnu/gcc/gcc-14.2.0/gcc-14.2.0.tar.xz
tar -xf gcc-14.2.0.tar.xz
```

### 3.3 Build Binutils

```bash
mkdir build-binutils && cd build-binutils
../binutils-2.43/configure --target=$TARGET --prefix="$PREFIX" \
    --with-sysroot --disable-nls --disable-werror
make -j$(nproc)
make install
```

### 3.4 Build GCC

```bash
cd ~/src
mkdir build-gcc && cd build-gcc
../gcc-14.2.0/configure --target=$TARGET --prefix="$PREFIX" \
    --disable-nls --enable-languages=c --without-headers --disable-hosted-libstdcxx
make all-gcc -j$(nproc)
make all-target-libgcc -j$(nproc)
make install-gcc
make install-target-libgcc
```

### 3.5 Verify

```bash
x86_64-elf-gcc --version
x86_64-elf-as --version
x86_64-elf-ld --version
```

Expected output: `x86_64-elf-gcc (GCC) 14.2.0`

---

## 4. Set Up QEMU + OVMF

### 4.1 Verify OVMF

```bash
ls /usr/share/ovmf/OVMF.fd
```

If missing, install `ovmf` package (already done above).

### 4.2 Test boot

```bash
qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd -cdrom /dev/null
```

Should show UEFI shell or "no bootable device" — confirms QEMU + UEFI works.

### 4.3 Install GDB (for debugging)

```bash
sudo apt install -y gdb
```

Or build `x86_64-elf-gdb` using the same `--target=$TARGET` configure as binutils.

---

## 5. Build the Cross-Compiler Using Script (Alternative)

The `toolchain/` directory in the repo contains `build-cross-compiler.sh`:

```bash
#!/bin/bash
# Build x86_64-elf cross-compiler inside WSL
# Run from repo root: bash toolchain/build-cross-compiler.sh

set -e

PREFIX="$HOME/opt/cross"
TARGET="x86_64-elf"
SYSROOT="$PREFIX/$TARGET"
...

```

This automates the steps above. Run it once and forget.

---

## 6. Working with Source Code

Source code lives on the Windows filesystem:
```
C:\Users\brightcat\Documents\axiomeOS\
```

Access from WSL at:
```
/mnt/c/Users/brightcat/Documents/axiomeOS/
```

### Recommended Workflow

1. **Edit** on Windows (VS Code, vim, etc.)
2. **Build** in WSL: `wsl` → `cd /mnt/c/.../axiomeOS` → `make`
3. **Run** in WSL: `make run` (launches QEMU window)
4. **Debug** in WSL: `make debug` (QEMU + GDB stub)

### Optional: Windows-side WSL wrapper

Create `build.bat` in the repo root:

```batch
@echo off
wsl make %*
```

Then `build run` works from Windows cmd/PowerShell.

---

## 7. Troubleshooting

| Symptom | Fix |
|---------|-----|
| `x86_64-elf-gcc: command not found` | `export PATH="$HOME/opt/cross/bin:$PATH"` or re-source `~/.bashrc` |
| QEMU: "Could not open OVMF.fd" | `sudo apt install ovmf` or find path with `dpkg -L ovmf` |
| `grub-mkrescue: command not found` | Install `grub-common` |
| `make: command not found` | `sudo apt install build-essential` |
| Permission denied on `/mnt/c/...` | Files are owned by `$USER`; check `wsl --status` for WSL 2 |
