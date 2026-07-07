# axiomeOS — Implementation Phases

> Derived from `tech-spec.md` v0.2 "bootstrap design document"
> Target: x86_64, GRUB multiboot2, ELF64, C

---

## Phase 0 — Development Environment & Toolchain

| # | Task | Depends On |
|---|------|-----------|
| 0.1 | Set up cross-compiler toolchain (x86_64-elf-gcc, binutils) in WSL | — |
| 0.2 | Configure linker script (`linker.ld`) for higher-half kernel | 0.1 |
| 0.3 | Create Makefile / build system | 0.1 |
| 0.4 | Set up QEMU + OVMF + grub-mkrescue in WSL | — |
| 0.5 | Write boot.S: multiboot2 header + early entry point | 0.1–0.3 |
| 0.6 | Write minimal linker.ld + grub.cfg for kernel | 0.2, 0.5 |
| 0.7 | Test: `make run` boots kernel in QEMU (black screen) | 0.4–0.6 |

**Deliverable:** `make run` boots an empty kernel in QEMU.

---

## Phase 1 — GRUB Multiboot2 Bootstrap

| # | Task | Depends On |
|---|------|-----------|
| 1.1 | Write grub.cfg: boot kernel.elf via multiboot2 | 0.6 |
| 1.2 | Implement make iso target: grub-mkrescue → axiome.iso | 1.1 |
| 1.3 | Parse multiboot2 info structure (magic number, tags, memory map) | 0.5 |
| 1.4 | Parse framebuffer tag from multiboot2 info | 1.3 |
| 1.5 | Retrieve ACPI RSDP + SMBIOS from multiboot2 info (or search) | 1.3 |
| 1.6 | Set up initial higher-half page tables (identity map + kernel map) | 1.3 |
| 1.7 | Set up GDT with ring 0 + ring 3 segments | 1.6 |
| 1.8 | Set up IDT with null handlers | 1.7 |
| 1.9 | Jump to kmain() | 1.8 |

**Deliverable:** Kernel boots via GRUB, framebuffer initialized, memory map parsed, GDT/IDT ready.

---

## Phase 2 — Early Kernel Bootstrap

| # | Task | Depends On |
|---|------|-----------|
| 2.1 | Write kmain() entry point in C | 1.9 |
| 2.2 | Initialize serial port (COM1) for early debug output | 2.1 |
| 2.3 | Write framebuffer text renderer (basic putchar on multiboot2 framebuffer) | 2.1 |
| 2.4 | Parse multiboot2 memory map into physical memory allocator (bitmap/frame stack) | 1.3 |
| 2.5 | Set up GDT (Global Descriptor Table) with ring 0 + ring 3 segments | 2.1 |
| 2.6 | Set up IDT (Interrupt Descriptor Table) with null handlers | 2.5 |
| 2.7 | Program APIC (local APIC timer, disable PIC) | 2.5 |
| 2.8 | Test: kernel prints "Hello, axiomeOS!" to framebuffer + serial | 2.1–2.7 |

**Deliverable:** Kernel boots, prints to screen, has interrupts ready.

---

## Phase 3 — Interrupts & Basic Drivers

| # | Task | Depends On |
|---|------|-----------|
| 3.1 | Implement exception handlers (page fault, GPF, double fault with stack) | 2.6 |
| 3.2 | Implement IRQ dispatcher (APIC I/O redirection) | 2.7 |
| 3.3 | Driver: PIT or HPET as system timer | 3.2 |
| 3.4 | Driver: PS/2 keyboard (polling → interrupt) | 3.2 |
| 3.5 | Driver: PS/2 mouse (interrupt-based) | 3.2 |
| 3.6 | Implement deferred work: bottom halves (softirq) | 3.1–3.2 |
| 3.7 | Implement spinlocks and IRQ-safe locking primitives | 3.6 |
| 3.8 | Test: keyboard input prints to framebuffer, timer ticks visible | 3.3–3.5 |

**Deliverable:** Interrupts working, keyboard input, timer, basic locks.

---

## Phase 4 — Memory Manager

| # | Task | Depends On |
|---|------|-----------|
| 4.1 | Physical page allocator (buddy system or bitmap) | 2.4 |
| 4.2 | Virtual memory manager (page table manipulation API) | 1.6, 4.1 |
| 4.3 | Kernel heap allocator (slab allocator for small objects) | 4.1 |
| 4.4 | Demand paging (page fault handler maps on demand) | 3.1, 4.2 |
| 4.5 | Copy-on-write support (fork optimization) | 4.4 |
| 4.6 | `mmap` backend: physical memory → virtual address space mapping | 4.2 |
| 4.7 | Test: allocate/free stress test, verify isolation | 4.1–4.6 |

**Deliverable:** Full virtual memory, kernel heap, demand paging, COW.

---

## Phase 5 — Scheduler & Multitasking

| # | Task | Depends On |
|---|------|-----------|
| 5.1 | Implement thread struct + context switch (assembly `switch_r3` for ring 3) | 2.5, 4.2 |
| 5.2 | Implement round-robin run queue (single core) | 5.1 |
| 5.3 | Timer interrupt → scheduler tick → yield | 3.3, 5.2 |
| 5.4 | Implement priority levels + interactive boost | 5.3 |
| 5.5 | Idle thread + halt-on-idle | 5.3 |
| 5.6 | Kernel threads (create, yield, exit) | 5.3 |
| 5.7 | SMP: detect APs via ACPI MADT, bring them online | 2.7, 5.6 |
| 5.8 | Per-CPU run queues + load balancing | 5.7 |
| 5.9 | Blocking/wait primitives (sleep, wait_for_interrupt) | 5.6 |
| 5.10 | Test: spawn 10 kernel threads, each printing to screen | 5.6 |

**Deliverable:** Multitasking kernel with preemptive scheduling on all cores.

---

## Phase 6 — Syscall Layer

| # | Task | Depends On |
|---|------|-----------|
| 6.1 | Define syscall ABI (numbering, argument passing via registers) | 2.5 |
| 6.2 | Implement `syscall` handler entry (ring 3 → ring 0 transition via `sysenter`/`int 0x80`) | 6.1 |
| 6.3 | Syscall: `debug_putchar` (userspace → screen) | 6.2 |
| 6.4 | Syscall: `sched_yield` | 6.2 |
| 6.5 | Syscall: `mmap` / `munmap` | 4.2, 6.2 |
| 6.6 | Syscall: `exit` | 6.2 |
| 6.7 | Test: ring-3 demo that calls `debug_putchar` via syscall | 6.2 |

**Deliverable:** Userspace can enter kernel via syscalls.

---

## Phase 7 — ELF Loader & Userspace

| # | Task | Depends On |
|---|------|-----------|
| 7.1 | Write ELF64 parser (load segments, relocate, resolve symbols) | 4.2, 6.1 |
| 7.2 | Implement `execve` syscall (load ELF, set up stack, argv, envp) | 7.1, 6.1 |
| 7.3 | Create initial userspace process (init process) | 7.2 |
| 7.4 | Set up ring 3 segments (user CS/SS via TSS) | 2.5, 7.3 |
| 7.5 | Implement `fork` or `spawn` syscall (create process from binary) | 7.2, 4.5 |
| 7.6 | Syscall: `waitpid` (zombie reaping) | 7.5 |
| 7.7 | Console TTY driver (keyboard input → terminal output) | 3.4, 2.3 |
| 7.8 | Test: compile small static ELF, exec it from kernel | 7.1–7.6 |

**Deliverable:** Userspace processes running, ELF binaries executable.

---

## Phase 8 — Minimal libc (axiome libc)

| # | Task | Depends On |
|---|------|-----------|
| 8.1 | Write `_start` CRT0 for userspace (call `main`, then `exit`) | 7.2 |
| 8.2 | Implement syscall wrappers (open/close/read/write stubs) | 6.2 |
| 8.3 | Implement `malloc`/`free` (bump allocator → slab for userspace) | 6.5 |
| 8.4 | string.h: `memcpy`, `memset`, `strlen`, `strcmp` | — |
| 8.5 | stdio.h: `printf`, `puts`, `getchar` (via TTY syscalls) | 8.2, 7.7 |
| 8.6 | `errno.h`, `stdlib.h` basics (`atoi`, `abort`) | 8.3 |
| 8.7 | Test: userspace app using libc prints formatted string | 8.1–8.5 |

**Deliverable:** A working (minimal) C standard library for userspace.

---

## Phase 9 — Coreutils (axiome coreutils)

| # | Task | Depends On |
|---|------|-----------|
| 9.1 | `cat` — read file / stdin → stdout | 10.x or 8.5 |
| 9.2 | `echo` — print args to stdout | 8.5 |
| 9.3 | `ls` — list directory contents | 10.x |
| 9.4 | `cp`, `mv`, `rm` — file manipulation | 10.x |
| 9.5 | `mkdir` — directory creation | 10.x |
| 9.6 | `ps` — list processes (via `/sys` or syscall) | 11.x |
| 9.7 | `kill` — send signal to process | 11.x |
| 9.8 | `sh` — minimal shell (read command, fork+exec, pipe) | 7.5, 7.2, 9.1–9.3 |

**Deliverable:** Bootable shell with basic file commands.

---

## Phase 10 — VFS & Filesystem

| # | Task | Depends On |
|---|------|-----------|
| 10.1 | Design VFS interface: `inode`, `dentry`, `file_operations`, `superblock` | — |
| 10.2 | Implement VFS layer (mount table, path resolution, open/read/write) | 10.1 |
| 10.3 | Implement `open`, `close`, `read`, `write`, `fstat` syscalls | 6.1, 10.2 |
| 10.4 | Implement `readdir`, `mkdir`, `unlink` syscalls | 10.2 |
| 10.5 | Ramdisk driver (simple initramfs for boot) | 10.2 |
| 10.6 | FAT32 driver (boot partition read/write) | 10.2 |
| 10.7 | axiomefs: on-disk format (journaling, ext attributes, B-tree directory) | 10.2 |
| 10.8 | axiomefs: create, mount, journal replay | 10.7 |
| 10.9 | Implement `mount` / `umount` syscalls | 10.2 |
| 10.10 | Syscall: `chdir`, `getcwd` | 10.2 |
| 10.11 | Syscall: `dup2`, `pipe` (for shell pipelines) | 10.2 |
| 10.12 | Test: create file, write, read back, delete | 10.3–10.5 |

**Deliverable:** Full VFS with FAT32 + axiomefs support, filesystem syscalls.

---

## Phase 11 — Process & IPC

| # | Task | Depends On |
|---|------|-----------|
| 11.1 | Signal framework (signal delivery, handler table, blocked masks) | 5.9 |
| 11.2 | Syscall: `kill`, `sigaction`, `sigreturn` | 11.1 |
| 11.3 | IPC channels (bounded buffer, message-passing send/recv) | 5.9 |
| 11.4 | Syscall: `ipc_create`, `ipc_send`, `ipc_recv` | 11.3 |
| 11.5 | Shared memory regions (`shm_open`, `mmap SHARED`) | 4.6 |
| 11.6 | Pipe implementation on top of VFS (or IPC) | 10.11 |
| 11.7 | `/proc` or `/sys` virtual filesystem (process info) | 10.2, 5.6 |
| 11.8 | Test: two processes communicate via IPC channel | 11.4 |

**Deliverable:** Inter-process communication, signals, shared memory.

---

## Phase 12 — Driver Framework

| # | Task | Depends On |
|---|------|-----------|
| 12.1 | Design driver interface (probe, init, ops table) | — |
| 12.2 | PCI enumeration (bus scan, vendor/device IDs) | 3.2 |
| 12.3 | NVMe driver (storage) | 12.2 |
| 12.4 | AHCI/SATA driver (storage fallback) | 12.2 |
| 12.5 | USB HCI driver (xHCI for keyboard/mouse) | 12.2 |
| 12.6 | Network driver skeleton (RTL8139 or e1000) | 12.2 |
| 12.7 | Loadable kernel module infrastructure (ELF .ko loading) | 6.2, 7.1 |
| 12.8 | Device tree / `/dev` filesystem | 10.2, 12.1 |
| 12.9 | Hotplug support (ACPI notification → driver probe) | 12.1 |

**Deliverable:** Driver framework, PCI, NVMe, USB input, device files.

---

## Phase 13 — Network Stack

| # | Task | Depends On |
|---|------|-----------|
| 13.1 | Minimal network buffer management (mbufs) | 12.6 |
| 13.2 | Ethernet driver binding + RX/TX ring | 12.6 |
| 13.3 | ARP implementation | 13.2 |
| 13.4 | IPv4 (minimal: send, receive, checksum) | 13.2 |
| 13.5 | ICMP (ping reply) | 13.4 |
| 13.6 | UDP socket (send/recv) | 13.4 |
| 13.7 | TCP socket (simplified: SYN/ACK, sliding window) | 13.4 |
| 13.8 | Syscall: `socket`, `bind`, `connect`, `send`, `recv` | 13.6–13.7 |
| 13.9 | DHCP client (obtain IP automatically) | 13.4 |
| 13.10 | DNS stub resolver | 13.9 |
| 13.11 | Test: ping another machine, HTTP GET via socket | 13.8 |

**Deliverable:** Basic TCP/IP networking from userspace.

---

## Phase 14 — Graphics & Compositor

| # | Task | Depends On |
|---|------|-----------|
| 14.1 | Kernel framebuffer driver (double-buffered) | 1.4 |
| 14.2 | 2D software renderer (rect, blit, font rendering, alpha blend) | 14.1 |
| 14.3 | Userspace window server (`axiome-ws`) process | 7.3, 14.2 |
| 14.4 | IPC protocol for window server (create window, draw, events) | 11.3, 14.3 |
| 14.5 | Compositor: surface management, damage tracking, VSync | 14.3 |
| 14.6 | Mouse cursor rendering + input event routing | 14.3, 3.5 |
| 14.7 | Keyboard focus management | 14.3, 3.4 |

**Deliverable:** GUI with windows, mouse cursor, keyboard input.

---

## Phase 15 — Desktop Environment

| # | Task | Depends On |
|---|------|-----------|
| 15.1 | Top menu bar (clock, app menu, status items) | 14.5 |
| 15.2 | Dock / launcher (app icons, click to launch) | 14.5 |
| 15.3 | Window manager (move, resize, minimize, close, stacking) | 14.5 |
| 15.4 | Desktop wallpaper / background rendering | 14.5 |
| 15.5 | File manager (basic: browse, open, copy/paste) | 10.3, 14.5 |
| 15.6 | Terminal emulator (GUI window with PTY) | 7.7, 14.5 |
| 15.7 | App launcher (spotlight-like search) | 14.5 |

**Deliverable:** Boot-to-desktop experience, usable GUI.

---

## Phase 16 — System Services & Polish

| # | Task | Depends On |
|---|------|-----------|
| 16.1 | Init system (`axiome-init`): starts services, manages lifecycle | 7.3 |
| 16.2 | Service manager (restart on crash, dependencies, logging) | 16.1 |
| 16.3 | Logging daemon (syslog-like, ring buffer + disk) | 16.1 |
| 16.4 | Sound: Intel HDA driver skeleton | 12.1 |
| 16.5 | Power management (ACPI sleep, shutdown, restart) | 12.1 |
| 16.6 | Timekeeping (RTC, NTP sync) | 13.9, 3.3 |
| 16.7 | User authentication (login screen, /etc/passwd) | 16.1 |
| 16.8 | Preferences/settings persistence | 10.3 |
| 16.9 | Installer (partition disk, copy system, configure boot) | 10.6–10.8 |

**Deliverable:** Complete, self-hosting OS.

---

## Dependency Graph (Condensed)

```
Phase 0  (toolchain + build)
   │
Phase 1  (GRUB multiboot2)
   │
Phase 2  (kernel bootstrap)
   │
   ├──────────────────────┐
Phase 3 (interrupts)      Phase 4 (memory manager)
   │                         │
   └──────────┬──────────────┘
              │
         Phase 5 (scheduler)
              │
         Phase 6 (syscalls)
              │
         Phase 7 (ELF loader + userspace)
              │
         ┌────┴────┐
    Phase 8     Phase 10
    (libc)       (VFS/FS)
         │        │
    Phase 9        │
   (coreutils)     │
         │         │
         └────┬────┘
              │
         Phase 11 (IPC)
              │
         ┌────┴────┐
    Phase 12    Phase 14
   (drivers)   (graphics)
         │        │
    Phase 13       │
   (network)       │
         │         │
         └────┬────┘
              │
         Phase 15 (desktop)
              │
         Phase 16 (services + polish)
```
