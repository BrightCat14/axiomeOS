#ifndef AXIOME_HAL_CSHIM_H
#define AXIOME_HAL_CSHIM_H

#include <stdint.h>
#include <stddef.h>

/* C-callable shims over the C++ HAL so existing C kernel code (and C
   device drivers) can use the hardware abstraction layer. Portable C code
   should call these functions instead of touching hardware directly. */

#ifdef __cplusplus
extern "C" {
#endif

/* Interrupt handler. ctx is the opaque context from register(); frame is the
   raw interrupted-CPU register frame (may be NULL if the arch does not
   provide one). The frame lets handlers see whether user mode was running. */
typedef void (*hal_irq_handler_t)(void *ctx, void *frame);

/* ---- Bootstrapping ---- */

void hal_init(void);

/* ---- Port-mapped I/O ---- */

uint8_t  hal_port_in8(uint16_t port);
void     hal_port_out8(uint16_t port, uint8_t val);
uint16_t hal_port_in16(uint16_t port);
void     hal_port_out16(uint16_t port, uint16_t val);
uint32_t hal_port_in32(uint16_t port);
void     hal_port_out32(uint16_t port, uint32_t val);
void     hal_port_ins16(uint16_t port, void *buf, uint32_t count);
void     hal_port_outs16(uint16_t port, const void *buf, uint32_t count);
void     hal_port_delay(void);

/* ---- CPU control ---- */

void     hal_cpu_halt(void);
void     hal_cpu_pause(void);
void     hal_cpu_irq_enable(void);
void     hal_cpu_irq_disable(void);
unsigned long hal_cpu_save_irq(void);
void     hal_cpu_restore_irq(unsigned long flags);
uint64_t hal_cpu_fault_address(void);
void     hal_cpu_tlb_flush(void);
void     hal_cpu_memory_barrier(void);

/* ---- Interrupts ---- */

int  hal_irq_register(int vector, hal_irq_handler_t fn, void *ctx);
int  hal_irq_unregister(int vector);
void hal_irq_dispatch(int vector, void *frame);
void hal_irq_eoi(void);
void hal_irq_mask(int irq, int masked);
void hal_irq_route(int irq, uint8_t vector, int masked);

/* ---- Timer ---- */

void     hal_timer_start(uint32_t period_ticks);
uint64_t hal_timer_ticks(void);

/* ---- Serial console ---- */

void hal_serial_init(uintptr_t base);
void hal_serial_putc(uintptr_t base, char c);
void hal_serial_write(uintptr_t base, const char *s);
char hal_serial_getc(uintptr_t base);
int  hal_serial_rx_ready(uintptr_t base);
void hal_serial_enable_input(uintptr_t base);

/* ---- MMIO ---- */

void *hal_mmio_map_phys(uint64_t phys, size_t size);

#ifdef __cplusplus
}
#endif

#endif
