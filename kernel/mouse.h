#ifndef AXIOME_MOUSE_H
#define AXIOME_MOUSE_H

#include <stdint.h>

struct mouse_event
{
    int dx;
    int dy;
    uint8_t buttons;
};

void mouse_init(void);
void mouse_irq_handler(void);
int mouse_read_event(struct mouse_event *ev);

#endif
