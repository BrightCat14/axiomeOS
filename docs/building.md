# Building axiomeOS

Before building, make sure the toolchain described in
[toolchain.md](toolchain.md) is installed.

---

# Build

Build everything:

```bash
make
```

This produces:

```
build/
├── axiome.iso
├── disk.img
└── kernel/
```

---

# Build Targets

## Kernel

Compile only the kernel:

```bash
make kernel
```

---

## ISO Image

Build the bootable installation ISO:

```bash
make iso
```

Output:

```
build/axiome.iso
```

---

## Disk Image

Create a bootable virtual disk:

```bash
make disk.img
```

The disk image contains:

- FAT32 boot partition
- axiomeFS root partition

Output:

```
build/disk.img
```

---

# Running

Launch QEMU:

```bash
make run
```

The VM uses:

- OVMF (UEFI)
- E1000 network adapter
- 512 MB RAM
- serial output to the terminal

---

## Framebuffer Mode

Run with a standard VGA framebuffer:

```bash
make run-fb
```

---

# Debugging

Start QEMU waiting for GDB:

```bash
make debug
```

Then connect:

```bash
gdb build/kernel/kernel.elf

(gdb) target remote :1234
```

---

# Cleaning

Remove temporary build files:

```bash
make clean
```

Remove the entire build directory:

```bash
make distclean
```

---

# Build Directory

All generated files are placed inside:

```
build/
```

The source tree itself is never modified during the build process.
