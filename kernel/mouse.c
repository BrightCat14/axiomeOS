#include "mouse.h"
#include "printk.h"
#include "softirq.h"
#include "io.h"
#include "hal/cshim.h"

#define MOUSE_IRQ 12

#define CMD_PORT 0x64
#define DATA_PORT 0x60

#define CMD_WRITE_AUX 0xD4
#define CMD_AUX_ENABLE 0xA8

#define AUX_SET_DEFAULTS 0xF6
#define AUX_ENABLE_DATA 0xF4
#define AUX_SET_SAMPLE 0xF3
#define AUX_SET_RES 0xE8

#define MOUSE_EVENT_BUF 32

static struct mouse_event evbuf[MOUSE_EVENT_BUF];
static volatile int evhead, evtail;
static uint8_t mouse_cycle;
static uint8_t mouse_packet[3];

static inline void wait_write(void)
{
    for (int i = 0; i < 10000; i++)
        if (!(inb(CMD_PORT) & 2))
            return;
}

static void aux_write(uint8_t val)
{
    wait_write();
    outb(CMD_PORT, CMD_WRITE_AUX);
    wait_write();
    outb(DATA_PORT, val);
}

static uint8_t aux_read(void)
{
    for (int i = 0; i < 10000; i++)
        if (inb(CMD_PORT) & 1)
            return inb(DATA_PORT);
    return 0;
}

static void mouse_process_packet(void)
{
    int dx = (int)(int8_t)mouse_packet[1];
    int dy = -(int)(int8_t)mouse_packet[2];
    uint8_t buttons = mouse_packet[0] & 7;

    if (mouse_packet[0] & 0x40) dx += 0xFFFFFF00;
    if (mouse_packet[0] & 0x80) dy -= 0xFFFFFF00;

    int next = (evhead + 1) % MOUSE_EVENT_BUF;
    if (next != evtail)
    {
        evbuf[evhead].dx = dx;
        evbuf[evhead].dy = dy;
        evbuf[evhead].buttons = buttons;
        evhead = next;
    }
}

static void mouse_poll(void *arg)
{
    (void)arg;
    struct mouse_event ev;
    while (mouse_read_event(&ev))
        printk("mouse: dx=%d dy=%d btns=%u\n", ev.dx, ev.dy, ev.buttons);
}

void mouse_irq_handler(void)
{
    uint8_t status = inb(CMD_PORT);
    if (status & 0x20)
    {
        uint8_t data = inb(DATA_PORT);
        mouse_packet[mouse_cycle] = data;
        mouse_cycle++;
        if (mouse_cycle >= 3)
        {
            mouse_cycle = 0;
            mouse_process_packet();
            softirq_schedule(mouse_poll, 0);
        }
    }
}

void mouse_init(void)
{
    printk("Mouse: probing...\n");

    outb(CMD_PORT, CMD_AUX_ENABLE);
    aux_write(AUX_SET_DEFAULTS);
    aux_read();

    aux_write(AUX_SET_SAMPLE);
    aux_read();
    aux_write(100);
    aux_read();

    aux_write(AUX_ENABLE_DATA);
    uint8_t ack = aux_read();
    if (ack != 0xFA)
    {
        printk("Mouse: no response (0x%x)\n", ack);
        return;
    }

    hal_irq_mask(MOUSE_IRQ, 0);
    printk("Mouse: ready\n");
}

int mouse_read_event(struct mouse_event *ev)
{
    if (evtail == evhead)
        return 0;
    *ev = evbuf[evtail];
    evtail = (evtail + 1) % MOUSE_EVENT_BUF;
    return 1;
}
