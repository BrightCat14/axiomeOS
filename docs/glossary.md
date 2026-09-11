# axiomeOS — Glossary

| Term | Definition |
|------|------------|
| **ACPI** | Advanced Configuration and Power Interface. Tables provided by firmware describing hardware topology. |
| **AHCI** | Advanced Host Controller Interface. Standard for SATA host controllers. |
| **APIC** | Advanced Programmable Interrupt Controller. Replaces the legacy 8259 PIC. Includes Local APIC (per CPU) and I/O APIC (system-wide). |
| **BSP** | Bootstrap Processor. The primary CPU that boots the system. |
| **COW** | Copy-on-Write. Optimization where pages shared after `fork()` are only copied when written to. |
| **CPL** | Current Privilege Level. Ring 0 (kernel) or Ring 3 (user). |
| **CS** | Code Segment. Segment register that contains the current privilege level in x86-64. |
| **ELF** | Executable and Linkable Format. The native binary format for axiomeOS. |
| **GDT** | Global Descriptor Table. Defines memory segments (minimal in x86-64: null, kernel code/data, user code/data, TSS). |
| **GOP** | Graphics Output Protocol. UEFI protocol for framebuffer access (used by the axboot loader). |
| **GPF** | General Protection Fault. Exception (#13) — access violation, ring violation, etc. |
| **axboot** | axiomeOS UEFI bootloader (`BOOTX64.EFI`) and its native 64-bit handoff protocol (`struct axboot_info`). Replaces GRUB/multiboot2. |
| **HPET** | High Precision Event Timer. High-resolution timer replacing PIT. |
| **HHDM** | Higher-Half Direct Map. Kernel virtual address space that directly maps all physical memory. |
| **IDT** | Interrupt Descriptor Table. Maps interrupt vectors to handler entry points. |
| **IOAPIC** | I/O APIC. Routes hardware interrupts from devices to one or more Local APICs. |
| **IPC** | Inter-Process Communication. Message passing and shared memory between processes. |
| **IRQ** | Interrupt Request. Hardware interrupt line. |
| **ISR** | Interrupt Service Routine. Code that handles an interrupt. |
| **LAPIC** | Local APIC. Per-CPU interrupt controller; handles timer, IPI, and external interrupts. |
| **MADT** | Multiple APIC Description Table. ACPI table describing APIC configuration (LAPICs, I/O APICs, interrupt mappings). |
| **MSR** | Model-Specific Register. CPU-specific control registers accessed via `rdmsr`/`wrmsr`. |
| **NMI** | Non-Maskable Interrupt. Interrupt that cannot be ignored (often used for hardware faults). |
| **NVMe** | Non-Volatile Memory Express. High-performance SSD interface over PCIe. |
| **OVMF** | Open Virtual Machine Firmware. UEFI firmware implementation for QEMU. |
| **PIC** | Programmable Interrupt Controller (8259). Legacy interrupt controller (disabled in favor of APIC). |
| **PIT** | Programmable Interval Timer (8253). Legacy timer chip (replaced by HPET/APIC timer). |
| **PMM** | Physical Memory Manager. Kernel subsystem that tracks available physical memory. |
| **PSF** | PC Screen Font. Simple bitmap font format used for framebuffer text. |
| **RSDP** | Root System Description Pointer. ACPI table entry point (found in UEFI config tables or legacy EBDA). |
| **RTC** | Real-Time Clock. Battery-backed clock for date/time. |
| **SMP** | Symmetric Multiprocessing. Multiple CPUs sharing memory and running the kernel. |
| **TSS** | Task State Segment. Per-CPU structure holding stack pointers for privilege transitions (IST stack for double faults, etc.). |
| **UEFI** | Unified Extensible Firmware Interface. Modern firmware standard replacing BIOS. |
| **VFS** | Virtual Filesystem. Kernel layer that provides a unified file API over multiple filesystem drivers. |
| **VMM** | Virtual Memory Manager. Kernel subsystem that manages page tables and address spaces. |
| **xHCI** | eXtensible Host Controller Interface. USB 3.x host controller standard. |
