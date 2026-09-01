#include "hal.h"
#include "hal_bootinfo.h"
#include "cshim.h"

/* Glue between the architecture-neutral HAL interfaces and the concrete
   per-architecture backends. Everything defined here is POD, so no static
   construction runs before hal_init() and no libstdc++ runtime is pulled
   into the kernel. */

namespace hal {

static HalPlatform g_platform = { 0, 0, 0, 0, 0, 0 };

HalPlatform &platform(void)
{
    return g_platform;
}

} /* namespace hal */

static struct hal_bootinfo g_bootinfo;

extern "C" void hal_init(void)
{
    static int done = 0;
    if (done)
        return;
    done = 1;
    hal_arch_init();
}

extern "C" struct hal_bootinfo *hal_bootinfo(void)
{
    return &g_bootinfo;
}

/* ---- Port-mapped I/O ---- */

extern "C" uint8_t hal_port_in8(uint16_t port) { return hal::port_io().in8(port); }
extern "C" void hal_port_out8(uint16_t port, uint8_t val) { hal::port_io().out8(port, val); }
extern "C" uint16_t hal_port_in16(uint16_t port) { return hal::port_io().in16(port); }
extern "C" void hal_port_out16(uint16_t port, uint16_t val) { hal::port_io().out16(port, val); }
extern "C" uint32_t hal_port_in32(uint16_t port) { return hal::port_io().in32(port); }
extern "C" void hal_port_out32(uint16_t port, uint32_t val) { hal::port_io().out32(port, val); }
extern "C" void hal_port_ins16(uint16_t port, void *buf, uint32_t count)
{
    hal::port_io().ins16(port, buf, count);
}
extern "C" void hal_port_outs16(uint16_t port, const void *buf, uint32_t count)
{
    hal::port_io().outs16(port, buf, count);
}
extern "C" void hal_port_delay(void) { hal::port_io().delay(); }

/* ---- CPU control ---- */

extern "C" void hal_cpu_halt(void) { hal::cpu().halt(); }
extern "C" void hal_cpu_pause(void) { hal::cpu().pause(); }
extern "C" void hal_cpu_irq_enable(void) { hal::cpu().irq_enable(); }
extern "C" void hal_cpu_irq_disable(void) { hal::cpu().irq_disable(); }
extern "C" unsigned long hal_cpu_save_irq(void) { return hal::cpu().save_and_disable_irqs(); }
extern "C" void hal_cpu_restore_irq(unsigned long flags) { hal::cpu().restore_irqs(flags); }
extern "C" uint64_t hal_cpu_fault_address(void) { return hal::cpu().fault_address(); }
extern "C" void hal_cpu_tlb_flush(void) { hal::cpu().tlb_flush(); }
extern "C" void hal_cpu_memory_barrier(void) { hal::cpu().memory_barrier(); }

/* ---- Interrupts ---- */

extern "C" int hal_irq_register(int vector, hal_irq_handler_t fn, void *ctx)
{
    return hal::irq().register_handler(vector, fn, ctx);
}
extern "C" int hal_irq_unregister(int vector)
{
    return hal::irq().unregister_handler(vector);
}
extern "C" void hal_irq_dispatch(int vector, void *frame) { hal::irq().dispatch(vector, frame); }
extern "C" void hal_irq_eoi(void) { hal::irq().eoi(); }
extern "C" void hal_irq_mask(int irq, int masked) { hal::irq().mask(irq, masked != 0); }
extern "C" void hal_irq_route(int irq, uint8_t vector, int masked)
{
    hal::irq().route(irq, vector, masked);
}

/* ---- Timer ---- */

extern "C" void hal_timer_start(uint32_t period_ticks) { hal::timer().start(period_ticks); }
extern "C" uint64_t hal_timer_ticks(void) { return hal::timer().ticks(); }

/* ---- Serial console ---- */

extern "C" void hal_serial_init(uintptr_t base) { hal::serial().init(base); }
extern "C" void hal_serial_putc(uintptr_t base, char c) { hal::serial().putc(base, c); }
extern "C" void hal_serial_write(uintptr_t base, const char *s) { hal::serial().write(base, s); }
extern "C" char hal_serial_getc(uintptr_t base) { return hal::serial().getc(base); }
extern "C" int hal_serial_rx_ready(uintptr_t base) { return hal::serial().rx_ready(base); }
extern "C" void hal_serial_enable_input(uintptr_t base) { hal::serial().enable_input(base); }

/* ---- MMIO ---- */

extern "C" void *hal_mmio_map_phys(uint64_t phys, size_t size)
{
    return hal::mmio().map_phys(phys, size);
}
