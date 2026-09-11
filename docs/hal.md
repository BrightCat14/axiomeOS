# axiomeOS — Hardware Abstraction Layer (HAL)

> Added in the HAL work (#20) to make axiomeOS portable across architectures.
> The HAL is the one C++ exception to the kernel's otherwise C-only rule; it is
> deliberately isolated under `kernel/hal/` and `kernel/arch/<arch>/hal/`.

---

## 1. What the HAL does

The HAL sits between architecture-neutral kernel code and the hardware. It
replaces direct `inb()`/`outb()`, inline `hlt`/`sti`/`cli`, APIC/IOAPIC
register poking and bootloader-structure parsing with a small set of abstract
interfaces. Portable code calls the interfaces (or their `hal_*` C shims); each
architecture provides one concrete backend.

The current (and only) port is **x86_64**.

### Layout

```
kernel/
├── hal/                     # Architecture-NEUTRAL, C++
│   ├── hal.h                # Umbrella + HalPlatform binding + placement new
│   ├── hal_portio.h         # IPortIo   — port-mapped I/O
│   ├── hal_cpu.h            # ICpu      — halt / irq / fault addr / tlb / barrier
│   ├── hal_irq.h            # IIrqController — dispatch table, EOI, mask, route
│   ├── hal_timer.h          # ITimer    — periodic tick timer
│   ├── hal_serial.h         # ISerial   — console UART
│   ├── hal_mmio.h           # IMmio     — map physical MMIO regions
│   ├── hal_bootinfo.h       # hal_bootinfo — portable boot environment
│   ├── cshim.h              # extern "C" hal_* shims for C callers
│   └── hal.cpp              # global binding + shim glue
│
└── arch/
    └── x86_64/
        ├── hal/
        │   ├── x86_hal.h    # backend factory declarations
        │   ├── x86_portio.cpp
        │   ├── x86_cpu.cpp
        │   ├── x86_irq.cpp
        │   ├── x86_timer.cpp
        │   ├── x86_serial.cpp
        │   ├── x86_mmio.cpp
        │   └── arch_init.cpp  # hal_arch_init(): bind backends
        ├── boot.S, isr.S, ... # arch boot + interrupt glue
        ├── apic.c, ioapic.c   # x86 interrupt hardware glue
        └── idt.c, isr_handlers.c
```

## 2. How to use the HAL from C

Existing kernel code stays C. It talks to the HAL through the C shims declared
in `kernel/hal/cshim.h`:

```c
#include "hal/cshim.h"

void irq_passthrough(void *ctx)
{
    (void)ctx;
    /* ... */
    hal_irq_eoi();          /* acknowledge */
}

void setup(void)
{
    uint8_t st = hal_port_in8(0x3F8 + 5);
    hal_port_out8(0x3F8, (uint8_t)'x');

    hal_cpu_irq_disable();
    hal_cpu_halt();
    hal_cpu_irq_enable();

    hal_irq_register(0x20, irq_passthrough, 0);   /* bind vector -> handler */
    hal_irq_mask(4, 0);                            /* unmask IRQ line 4 */
    hal_irq_route(4, 0x24, 0);                     /* route IRQ4 -> vector 0x24 */

    void *mapped = hal_mmio_map_phys(0xFEE00000ULL, 0x1000);

    hal_serial_write(COM1, "hello via HAL\n");
    hal_timer_start(0x10000);                      /* start the periodic timer */
}
```

C++ code can use the interfaces directly (see the x86_64 backends in
`kernel/arch/x86_64/hal/`):

```cpp
#include "hal/hal.h"
void f()
{
    hal::port_io().out8(0x3F8, 'a');
    uint64_t t = hal::timer().ticks();
    hal::cpu().halt();
}
```

The old `kernel/io.h` (`inb`/`outb`/...) now forwards to the HAL, so existing
drivers (IDE, PCI, FAT32, driver.c) are portable without source changes.

## 3. C++ in the kernel

The HAL compiles with the same freestanding flags as the C kernel plus:

```
-fno-exceptions -fno-rtti -fno-threadsafe-statics
-fno-use-cxa-atexit -fno-unwind-tables -fno-asynchronous-unwind-tables
```

- **No** heap, no libstdc++, no exceptions, no RTTI.
- Backends are constructed into **static storage** with placement new
  (`hal.h` provides the minimal `operator new(size_t, void*)`), so no
  `.init_array` / constructor runner is needed in the boot path.
- Nothing is ever destroyed; a sized-delete stub is provided because virtual
  destructors may reference it.
- The linker script already covers C++ sections (`.text*`, `.rodata*`,
  `.data*`), so **no linker changes are required**.

## 4. Porting to a new architecture

To add, say, RISC-V:

1. **Boot glue** — bring the CPU to the kernel, pass the boot environment:
   fill `hal_bootinfo` (framebuffer, mmap, etc.) exactly like
   `kernel/axboot.c` does on x86_64.
2. **Backends** — create `kernel/arch/riscv64/hal/` with one `.cpp` per
   interface (`riscv_portio.cpp`, `riscv_cpu.cpp`, ...) or, for memory-mapped
   SoCs, implement `IMmio` and leave `IPortIo` as a stub.
3. **`hal_arch_init()`** — provide it in
   `kernel/arch/riscv64/hal/arch_init.cpp`, constructing each backend into
   static storage and filling `hal::platform()` (see the x86_64 copy).
4. **Device-vector glue** — the arch ISR entry calls `hal_irq_dispatch(vector)`
   for device interrupts; register your handlers with
   `hal_irq_register(vector, fn, ctx)`.
5. **Build** — point `CC`/`CXX`/`AS`/`LD` in `kernel/Makefile` at the new
   toolchain and add the new `arch/riscv64/hal/*.cpp` wildcard.

### Checklist for a complete port

| Interface            | Required | Notes                                   |
|----------------------|----------|------------------------------------------|
| `hal_portio()`       | optional | Memory-mapped systems can stub it        |
| `hal_cpu()`          | yes      | halt, irq enable/disable, fault address  |
| `hal_irq()`          | yes      | handler table, EOI, mask, route          |
| `hal_timer()`        | yes      | scheduler tick source                    |
| `hal_serial()`       | yes      | boot console                             |
| `hal_mmio()`         | yes      | needed by virtually every driver         |
| `hal_bootinfo()`     | yes      | filled by your boot glue                 |

## 5. Interrupt flow (x86_64)

```
device ──> IOAPIC ──> Local APIC ──> IDT gate ──> isr.S (isr_*)
        isr_handler(struct isr_frame*)          (kernel/isr_handlers.c)
            │  vector >= 0x20
            └──> hal_irq_dispatch(vector)       (kernel/arch/x86_64/hal/x86_irq.cpp)
                    └──> registered fn(ctx)     (timer/kbd/mouse/serial wrappers)
                            └──> hal_irq_eoi()  -> apic_eoi()
```

Handlers are bound in `isr_init()` (`kernel/isr_handlers.c`) via
`hal_irq_register()`. The timer, PS/2 and serial wrappers preserve the exact
EOI ordering of the old hardcoded dispatch.

## 6. What was migrated

| Piece | Old               | New                                   |
|-------|-------------------|---------------------------------------|
| Serial (16550) | `kernel/serial.c` inlined PIO | `X86Serial` backend; `serial.c` is a shim |
| Port I/O | `kernel/io.h` inline asm | `IPortIo` backend; `io.h` forwards |
| Timer | `apic_init()` started it inline | `hal_timer_start()` -> `apic_init()` + `apic_timer_start()` |
| Device IRQs | hardcoded vectors in `isr_handler()` | `hal_irq_register()` / `hal_irq_dispatch()` |
| IRQ masking | `ioapic_mask()` in drivers | `hal_irq_mask()` |
| MMIO mapping | `pd_table3[]` pokes in apic/ioapic | `hal_mmio_map_phys()` (`X86Mmio`) |
| CPU control | `hlt`/`sti`/`cli`/CR2 inline asm | `hal_cpu_*()` (`X86Cpu`) |
| Boot environment | axboot structs only | also mirrors into `hal_bootinfo` |

