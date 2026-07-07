# axiomeOS — Milestone Roadmap

> High-level milestones derived from the 16-phase implementation plan.

---

## M0 — Bootloader + Kernel Entry

| Phase(s) | Milestone | Demo |
|----------|-----------|------|
| 0, 1 | Toolchain + GRUB multiboot2 entry | `make run` boots kernel in QEMU |

**Done when**: GRUB menu appears → "axiomeOS" selected → kernel prints message to screen.

---

## M1 — Kernel Bootstrap

| Phase(s) | Milestone | Demo |
|----------|-----------|------|
| 2, 3 | CPU init + interrupts + basic drivers | Keyboard echo, timer tick visible |

**Done when**: Kernel prints "Hello, axiomeOS!", keyboard input is echoed, timer increments a counter.

---

## M2 — Memory Management

| Phase(s) | Milestone | Demo |
|----------|-----------|------|
| 4 | Physical + virtual memory, heap allocator, demand paging | Memory stress test passes |

**Done when**: `kmalloc`/`kfree` stress test completes without corruption, page faults are handled gracefully.

---

## M3 — Multitasking

| Phase(s) | Milestone | Demo |
|----------|-----------|------|
| 5, 6, 7 | Scheduler + syscalls + ELF loader + userspace | Multiple userspace processes running |

**Done when**: Kernel boots → spawns init process → init spawns a shell. User can type commands.

---

## M4 — Userspace Environment

| Phase(s) | Milestone | Demo |
|----------|-----------|------|
| 8, 9 | libc + coreutils + shell | Working shell with `ls`, `cat`, `echo` |

**Done when**: Boot to shell, run `ls` to list ramdisk files, `echo hello` prints hello.

---

## M5 — Filesystem

| Phase(s) | Milestone | Demo |
|----------|-----------|------|
| 10, 11 | VFS + FAT32 + axiomefs + IPC | Files persist across reboot (via disk image) |

**Done when**: `touch test.txt` → `echo hello > test.txt` → `cat test.txt` → `hello`

---

## M6 — Drivers

| Phase(s) | Milestone | Demo |
|----------|-----------|------|
| 12, 13 | PCI, NVMe, USB HID, network stack | Network ping works |

**Done when**: Device files in `/dev`, USB keyboard works, `ping` responds to ICMP echo.

---

## M7 — Graphics

| Phase(s) | Milestone | Demo |
|----------|-----------|------|
| 14 | Framebuffer compositor + window server | GUI visible with windows and mouse cursor |

**Done when**: Boot to graphical desktop with a menu bar, mouse cursor follows mouse, windows can be moved.

---

## M8 — Desktop Environment

| Phase(s) | Milestone | Demo |
|----------|-----------|------|
| 15, 16 | Desktop, services, installer | Self-hosting OS with installer |

**Done when**: Boot-to-desktop experience, settings persist, installer can write OS to disk.

---

## Dependency Flow

```
M0: Boot + Entry
 │
M1: CPU + Interrupts
 │
M2: Memory Management
 │
M3: Multitasking + Userspace
 │
┌──┴──┐
M4    M5
libc  Filesystem
 │     │
 └──┬──┘
    │
   M6: Drivers + Network
    │
   M7: Graphics
    │
   M8: Desktop
```

---

## Timing Estimates (Rough)

| Milestone | Estimated effort | Dependencies |
|-----------|-----------------|-------------|
| M0 | 1–2 sessions | WSL setup, cross-compiler |
| M1 | 2–3 sessions | GDT, IDT, APIC, keyboard |
| M2 | 2–3 sessions | PMM, VMM, slab |
| M3 | 3–4 sessions | Scheduler, syscalls, ELF loader |
| M4 | 2–3 sessions | libc, coreutils, shell |
| M5 | 3–4 sessions | VFS, FAT32, axiomefs |
| M6 | 4–6 sessions | PCI, NVMe, USB, network stack |
| M7 | 3–4 sessions | Compositor, window server |
| M8 | 3–4 sessions | Desktop, services, installer |

Each "session" is roughly a focused coding session. Total: ~25–35 sessions for a complete OS.

*Note: These are optimistic estimates. OS development is unpredictable — bugs in low-level code can take days to debug.*
