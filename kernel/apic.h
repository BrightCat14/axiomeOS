#ifndef AXIOME_APIC_H
#define AXIOME_APIC_H

#include <stdint.h>

void apic_init(void);
void apic_eoi(void);
void apic_timer_tick(void);
uint64_t apic_get_ticks(void);

#endif
