#include "irq.h"
#include "idt.h"
#include "spinlock.h"
#include "string.h"

struct irq_slot {
    irq_handler_t handler;
    void *priv;
};

static struct irq_slot g_irq_table[IRQ_COUNT];
static spinlock_t g_irq_lock = SPINLOCK_INIT;

int irq_register(uint8_t vector, irq_handler_t handler, void *priv)
{
    if (vector < IRQ_BASE || vector > IRQ_MAX)
        return -1;
    if (!handler)
        return -1;

    spin_lock(&g_irq_lock);
    int idx = vector - IRQ_BASE;
    if (g_irq_table[idx].handler)
    {
        spin_unlock(&g_irq_lock);
        return -1;
    }
    g_irq_table[idx].handler = handler;
    g_irq_table[idx].priv = priv;
    spin_unlock(&g_irq_lock);
    return 0;
}

void irq_unregister(uint8_t vector)
{
    if (vector < IRQ_BASE || vector > IRQ_MAX)
        return;
    int idx = vector - IRQ_BASE;
    spin_lock(&g_irq_lock);
    g_irq_table[idx].handler = 0;
    g_irq_table[idx].priv = 0;
    spin_unlock(&g_irq_lock);
}

int irq_dispatch(struct isr_frame *frame)
{
    if (frame->int_no < IRQ_BASE || frame->int_no > IRQ_MAX)
        return -1;

    int idx = frame->int_no - IRQ_BASE;
    struct irq_slot *s = &g_irq_table[idx];
    if (!s->handler)
        return -1;

    s->handler(frame, s->priv);
    return 0;
}

void irq_init(void)
{
    memset(g_irq_table, 0, sizeof(g_irq_table));
}
