#ifndef AXIOME_KEYBOARD_H
#define AXIOME_KEYBOARD_H

#include <stdint.h>

void keyboard_init(void);
void keyboard_irq_handler(void);
void keyboard_hid_boot_report(uint8_t modifiers, const uint8_t keys[6]);

#endif
