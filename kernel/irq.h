#ifndef AXIOME_IRQ_H
#define AXIOME_IRQ_H

#include <stdint.h>

struct isr_frame;

#define IRQ_BASE      0x20
#define IRQ_COUNT     224
#define IRQ_MAX       (IRQ_BASE + IRQ_COUNT - 1)

typedef void (*irq_handler_t)(struct isr_frame *frame, void *priv);

int  irq_register(uint8_t vector, irq_handler_t handler, void *priv);
void irq_unregister(uint8_t vector);
int  irq_dispatch(struct isr_frame *frame);
void irq_init(void);

#endif
