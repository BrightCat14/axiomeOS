#include "idt.h"

extern struct idt_entry idt_table[256];

void idt_set_gate(uint8_t vector, uintptr_t handler, uint8_t flags, uint8_t ist)
{
    idt_table[vector].offset_low = handler & 0xFFFF;
    idt_table[vector].selector = 0x18;
    idt_table[vector].ist = (ist & 7) << 2;
    idt_table[vector].flags = flags;
    idt_table[vector].offset_mid = (handler >> 16) & 0xFFFF;
    idt_table[vector].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt_table[vector].reserved = 0;
}
