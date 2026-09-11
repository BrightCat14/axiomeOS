# axiomeOS — Directory Structure

> Target: x86_64, UEFI + custom axboot handoff, ELF64
> Toolchain: x86_64-elf-gcc (cross-compiler from WSL)

---

## Top-Level Layout

```
axiomeOS/
├── bootloader/             # Custom UEFI loader (gnu-efi BOOTX64.EFI, axboot)
│   ├── main.c              # efi_main: GOP → ACPI → ESP → ELF → EBS → jump
│   ├── gop.c / mmap.c      # framebuffer capture / memory-map conversion
│   ├── acpi.c / fs.c       # RSDP discovery / \kernel.elf loading
│   ├── elf.c / boot.c      # PT_LOAD placement / page tables + handoff
│   └── Makefile
├── include/                # Shared headers (axboot.h handoff protocol)
├── kernel/                 # Kernel source (ring 0)
│   ├── arch/x86_64/        # CPU/architecture-specific code
│   ├── mm/                 # Memory management
│   ├── sched/              # Scheduler and multitasking
│   ├── fs/                 # VFS and filesystem drivers
│   ├── drivers/            # Hardware drivers (PCI, storage, input, etc.)
│   ├── ipc/                # Inter-process communication
│   ├── lib/                # Kernel utility library (string, printf, etc.)
│   └── Makefile            # Kernel build rules
├── libc/                   # Axiome libc (freestanding, kernel + userland)
│   ├── include/            # Public headers (<stdio.h>, <stdlib.h>, etc.)
│   ├── src/                # Implementation
│   └── Makefile
├── userland/               # Userspace programs (ring 3)
│   ├── init/               # /init process
│   ├── coreutils/          # ls, cat, cp, mv, rm, mkdir, ps, sh
│   ├── window-server/      # GUI compositor (Phase 14+)
│   └── common/             # Shared userland libraries
├── build/                  # Build artifacts (gitignored)
├── images/                 # ISO/disk images (gitignored)
├── toolchain/              # Cross-compiler build scripts for WSL
├── docs/                   # All project documentation
│   ├── structure.md        # This file
│   ├── tech-spec.md        # Architecture specification
│   ├── phases.md           # Implementation phases
│   ├── toolchain.md        # WSL + cross-compiler setup
│   ├── build.md            # Build system documentation
│   ├── conventions.md      # Coding style and rules
│   ├── testing.md          # Verification approach
│   ├── glossary.md         # OS terms reference
│   └── roadmap.md          # High-level milestones
├── osdev_wiki/             # Local OSDev Wiki copy (git LFS recommended)
├── Makefile                # Top-level build orchestrator
├── linker.ld               # Kernel linker script (higher-half)
├── AGENTS.md               # AI agent instructions
└── README.md               # Project overview
```

---

## Kernel Directory (`kernel/`)

```
kernel/
├── hal/                    # Hardware abstraction layer (C++, arch-neutral)
│   ├── hal.h               # Umbrella header + hal::platform() binding
│   ├── hal_portio.h        # IPortIo    — port-mapped I/O
│   ├── hal_cpu.h           # ICpu       — halt / irq / barriers
│   ├── hal_irq.h           # IIrqController — handlers, EOI, mask, route
│   ├── hal_timer.h         # ITimer     — periodic tick timer
│   ├── hal_serial.h        # ISerial    — console UART
│   ├── hal_mmio.h          # IMmio      — map physical MMIO
│   ├── hal_bootinfo.h      # hal_bootinfo — portable boot environment
│   ├── cshim.h             # extern "C" hal_* shims for C callers
│   └── hal.cpp             # global binding + shim glue
├── arch/
│   └── x86_64/
│       ├── hal/            # x86_64 HAL backends (C++)
│       │   ├── x86_portio.cpp
│       │   ├── x86_cpu.cpp
│       │   ├── x86_irq.cpp
│       │   ├── x86_timer.cpp
│       │   ├── x86_serial.cpp
│       │   ├── x86_mmio.cpp
│       │   └── arch_init.cpp
│       ├── boot.S          # Native 64-bit axboot entry point
│       ├── idt.c/.h        # Interrupt Descriptor Table
│       ├── isr.S           # Interrupt service routines (assembly stubs)
│       ├── isr_handlers.c  # Exceptions + device vector dispatch
│       ├── apic.c/.h       # Local APIC + timer
│       ├── ioapic.c/.h     # I/O APIC
│       ├── switch.S        # Context switch (ring 0 and ring 3)
│       ├── syscall.S
│       ├── tss.c/.h        # Task state segment
│       └── serial.c        # C shim over hal::serial()
├── mm/
│   ├── pmm.c                   # Physical memory manager (bitmap/buddy)
│   ├── pmm.h
│   ├── vmm.c                   # Virtual memory manager
│   ├── vmm.h
│   ├── slab.c                  # Slab allocator for small kernel objects
│   ├── slab.h
│   └── kmap.h                  # Higher-half mapping constants
├── sched/
│   ├── sched.c                 # Scheduler core (run queues, priorities)
│   ├── sched.h
│   ├── thread.c                # Thread structure + lifecycle
│   ├── thread.h
│   ├── context.S               # Context switch (ring 0 and ring 3)
│   ├── mutex.c                 # Mutex / spinlock primitives
│   └── mutex.h
├── fs/
│   ├── vfs.c                   # Virtual filesystem layer
│   ├── vfs.h
│   ├── ramfs.c                 # initramfs / ramdisk driver
│   ├── ramfs.h
│   ├── fat.c                   # FAT32 driver (boot partition)
│   ├── fat.h
│   └── axiomefs.c/.h           # Native axiomefs (Phase 10)
├── drivers/
│   ├── pci.c                   # PCI/PCIe bus enumeration
│   ├── pci.h
│   ├── nvme.c                  # NVMe storage driver
│   ├── ahci.c                  # AHCI/SATA storage driver
│   ├── ps2kbd.c                # PS/2 keyboard driver
│   ├── ps2mouse.c              # PS/2 mouse driver
│   ├── xhci.c                  # xHCI USB driver
│   ├── framebuffer.c           # Framebuffer compositor
│   └── ...
├── ipc/
│   ├── channel.c               # Message-passing channels
│   ├── channel.h
│   ├── shm.c                   # Shared memory regions
│   └── shm.h
├── lib/
│   ├── string.c                # memcpy, memset, strlen, strcmp, etc.
│   ├── string.h
│   ├── printf.c                # Kernel printf (to serial + framebuffer)
│   ├── printf.h
│   ├── bitmap.c                # Generic bitmap operations
│   ├── bitmap.h
│   ├── list.h                  # Doubly-linked list macros
│   └── hash.h                  # Hash table helpers
├── syscall.c                   # Syscall dispatch table
├── syscall.h
├── elf.c                       # ELF64 loader (for user programs)
├── elf.h
├── kernel.c                    # kmain(axboot_info*) — kernel entry point
├── kernel.h                    # Master kernel header (includes all subsystem headers)
├── axboot.c                    # axboot handoff parsing (mmap/fb/ACPI)
└── Makefile                    # Kernel build rules
```

## Libc Directory (`libc/`)

```
libc/
├── include/
│   ├── stdio.h
│   ├── stdlib.h
│   ├── string.h
│   ├── errno.h
│   ├── unistd.h                # Syscall wrappers
│   ├── signal.h
│   ├── sys/
│   │   ├── types.h
│   │   └── mm.h
│   └── ...
├── src/
│   ├── stdio/
│   │   ├── printf.c
│   │   ├── putchar.c
│   │   └── getchar.c
│   ├── stdlib/
│   │   ├── malloc.c
│   │   ├── atoi.c
│   │   └── abort.c
│   ├── string/
│   │   ├── memcpy.c
│   │   ├── memset.c
│   │   ├── strlen.c
│   │   └── strcmp.c
│   └── syscall.c               # Syscall stubs (assembly)
├── crt0.S                      # _start entry point for user programs
└── Makefile
```

## Userland Directory (`userland/`)

```
userland/
├── init/
│   └── init.c                  # /init — first user process
├── coreutils/
│   ├── cat.c
│   ├── echo.c
│   ├── ls.c
│   ├── cp.c
│   ├── mv.c
│   ├── rm.c
│   ├── mkdir.c
│   ├── ps.c
│   ├── kill.c
│   ├── top.c
│   └── sh.c                    # Minimal shell
├── window-server/              # Phase 14+
│   ├── ws.c                    # Window server process
│   ├── compositor.c
│   ├── renderer.c
│   └── ...
└── Makefile                    # Builds all userland binaries
```

---

## Build Artifacts (`build/`)

```
build/
├── kernel/
│   ├── kernel.elf              # Final kernel binary
│   └── *.o                     # Object files
├── bootloader/
│   └── BOOTX64.EFI             # UEFI bootloader binary
├── libc/
│   └── libc.a                  # Static libc archive
├── userland/
│   ├── init.elf
│   ├── sh.elf
│   └── ...
├── boot.fat                    # FAT32 ESP (EFI/BOOT/BOOTX64.EFI + kernel.elf)
├── isowork/esp.img             # El Torito boot image (= boot.fat copy)
└── ...
```

---

## Key Path Constants

| Symbol | Value | Description |
|--------|-------|-------------|
| `KERNEL_PHYS_BASE` | `0x200000` | Physical load address (2 MiB, axboot loader target) |
| `KERNEL_VIRT_BASE` | `0xFFFFFFFF80000000` | Higher-half kernel virtual address |
| `HHDM_OFFSET` | `-KERNEL_VIRT_BASE` | Direct physical map offset |
| `STACK_SIZE` | `0x4000` (16 KiB) | Kernel stack size |
| `PAGE_SIZE` | `0x1000` (4 KiB) | Standard page size |
| `USER_STACK_SIZE` | `0x800000` (8 MiB) | Userspace stack size |

---

## Include Path Convention

- Kernel internal headers: `#include <kernel/mm/pmm.h>`
- Kernel arch headers: `#include <kernel/arch/x86_64/apic.h>`
- Kernel lib: `#include <kernel/lib/string.h>`
- Libc public headers: `#include <stdio.h>`
- Userspace headers: `#include <libc/stdio.h>` (when building freestanding)

A single `-I` include root at the repo top-level allows all above paths.
