axiomeos — technical specification (t0)

version: 0.2 “bootstrap design document”
target architecture: x86_64 (UEFI + axboot handoff; supersedes the original grub multiboot2 plan)
binary format: elf64 executable kernel + elf64 userland

---

# 1. overview

axiomeos is a unix-like operating system designed for modern uefi x86_64 machines with a clean separation between:

* kernel (axiome kernel)
* userland (axiome core utilities + libc)
* graphical environment (axiome ui)
* system services (process, fs, network, device abstraction)

design goals:

* full multitasking (preemptive + cooperative hybrid)
* elf64 native execution
* macos-like user experience (ui + filesystem feel)
* unix-posix inspired semantics (but not strictly posix)
* minimal legacy constraints (no bios, no 16-bit nonsense)

boot stack is based on the custom axboot UEFI bootloader (BOOTX64.EFI, gnu-efi)
using the native 64-bit axboot handoff protocol on uefi x86_64 systems.

---

# 2. boot process

## 2.1 axboot stage

* uefi firmware loads the axboot loader (`EFI/BOOT/BOOTX64.EFI`)
* the loader reads `\kernel.elf` from the esp via SimpleFileSystem
* the loader parses `kernel.elf` and loads PT_LOAD segments at `p_paddr`
* the loader provides (in one `struct axboot_info`, see `include/axboot.h`):

  * memory map (converted from the EFI memory map)
  * framebuffer (GOP-based, preferably 1024x768x32 RGB)
  * acpi rsdp + smbios tables
* the loader exits boot services, installs identity + higher-half page
  tables and jumps to the 64-bit kernel entry with `RDI` = bootinfo address

## 2.2 kernel entry

* `_start` in `boot.S` (native 64-bit axboot entry, no mode switch)
* set up stack pointer
* clear bss
* parse the axboot info structure (memory map, framebuffer, ACPI)
* set up:

  * gdt (global descriptor table)
  * idt (interrupt descriptor table)
  * higher-half paging (4-level page tables)
* enable apic / x2apic
* call `kmain()`

---

# 3. kernel architecture (axiome kernel)

## 3.1 type

monolithic kernel with modular subsystems (loadable kernel modules optional)

not microkernel (performance reasons), but strict internal APIs

---

## 3.2 core subsystems

* scheduler
* memory manager
* vfs (virtual filesystem layer)
* ipc system
* driver framework
* syscall interface
* graphics subsystem (kernel-assisted framebuffer compositor)

---

## 3.3 scheduling model

* preemptive multitasking
* per-core run queues (smp-ready)
* priority + fair scheduling hybrid:

  * interactive boost for gui
  * background batch throttling

task types:

* kernel threads
* user processes
* coroutine tasks (lightweight fibers inside process)

---

## 3.4 interrupts

* idt-based interrupt handling
* irq routing through apic
* deferred work system:

  * bottom halves (softirq-like)
  * work queues

interrupt categories:

* timer interrupt (scheduler tick or tickless mode)
* io interrupts (disk, network)
* ipc interrupts (fast wakeups)
* fault handlers (page fault, general protection)

---

# 4. memory management

* paging: x86_64 4-level paging
* heap allocator:

  * kernel slab allocator
  * buddy allocator for physical pages
* user memory isolation:

  * per-process address space
  * copy-on-write fork support

features:

* demand paging (optional in v1)
* mmap-style file mapping
* shared memory regions for ipc + gui

---

# 5. process model

process = isolated execution context containing:

* virtual address space
* file descriptor table
* thread pool
* coroutine scheduler
* ipc endpoints

syscalls:

* fork (optional, can be replaced with spawn)
* execve (elf loader)
* exit
* wait
* mmap / munmap
* ipc_send / ipc_recv

---

# 6. userland

## 6.1 libc (axiome libc)

custom libc designed for kernel + userland symmetry:

* stdio minimal layer
* memory (malloc/free over kernel heap service)
* string + utils
* syscalls wrapper
* coroutine primitives

no dependency on glibc.

---

## 6.2 coreutils (axiome coreutils)

basic unix-like tooling:

* ls
* cp
* mv
* rm
* mkdir
* cat
* ps
* kill
* top
* mount
* umount

design principle: small, composable, elf-native binaries.

---

## 6.3 coroutine system

first-class fibers:

* stackful coroutines inside process
* async scheduler integrated into libc
* used for:

  * io
  * gui events
  * networking

---

# 7. filesystem (axiomefs)

macos-inspired hierarchy:

* `/`
* `/Applications`
* `/Users`
* `/System`
* `/Volumes`

design:

* single unified namespace (vfs layer)
* supports multiple backends:

  * axiomefs native
  * fat32 (boot)
  * ext4 (optional compatibility)

features:

* journaling (mandatory for axiomefs)
* extended attributes (metadata tags like macos)
* case-preserving (not case-sensitive by default)
* path-based permissions

---

# 8. graphics system

## 8.1 framebuffer base

* initial mode: grub-provided framebuffer (gop via uefi)
* kernel maps framebuffer memory
* software compositor (no dependency on gpu initially)

## 8.2 gui architecture

* retained-mode UI system
* window server in user space (axiome-ws)
* compositor:

  * compositing surfaces per process
  * vsync driven rendering loop

ui design:

* macos-like:

  * top menu bar
  * dock-like launcher
  * global window manager rules

input:

* keyboard + mouse via interrupt drivers
* event bus delivered through ipc

---

# 9. driver model

* kernel driver framework (module-based)
* drivers run in kernel space initially (performance-first design)

device categories:

* storage (nvme, sata emulation)
* input (usb hid)
* display (framebuffer + future gpu drivers)
* network (ethernet/wifi abstraction)

hotplug support via acpi notification + pci enumeration

---

# 10. elf execution model

axiomeos uses elf64 as native format:

* kernel: elf64 executable loaded by grub via multiboot2
* user programs: elf64 shared or relocatable binaries

loader responsibilities:

* relocation
* symbol resolution (minimal dynamic linking in v1)
* setting up stack + argv/envp
* mapping libc runtime

---

# 11. ipc system

core design: message-passing first

* channels (typed message pipes)
* shared memory for high-performance transfers
* signals for async events

used for:

* gui events
* filesystem requests
* device communication

---

# 12. security model

* user/kernel separation (ring 0 / ring 3)
* no direct hardware access from userland
* syscall gate only entry point
* memory isolation enforced by paging

optional future:

* capability-based security layer
* sandboxed applications (macos-like app containers)

---

# 13. system services

* init system (axiome-init)
* service manager (lightweight, not systemd-like complexity)
* logging daemon
* device manager
* network stack service

---

# 14. development philosophy

* written in C + minimal assembly
* minimal abstraction layers
* “simple but not primitive”
* performance > theoretical purity
* stable internal kernel ABI (not userland ABI lock-in)

---

# 15. roadmap (milestones)

m0:

* grub multiboot2 bootstrap
* kernel loads + prints framebuffer text

m1:

* interrupts + scheduler
* basic multitasking

m2:

* elf loader + userland
* libc + coreutils

m3:

* vfs + axiomefs prototype

m4:

* gui compositor + window system

m5:

* driver framework + usb input

m6:

* usable desktop environment

---

# end note

axiomeos is basically: "linux brain + macos ui taste + grub multiboot2 + modern elf userland", built from scratch with fewer historical constraints and more explicit system design boundaries.

