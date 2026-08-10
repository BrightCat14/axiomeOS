#ifndef AXIOME_APIC_H
#define AXIOME_APIC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void apic_init(void);
void apic_timer_start(uint32_t count);
void apic_eoi(void);
void apic_timer_tick(void);
uint64_t apic_get_ticks(void);
uint64_t apic_get_ticks_per_ms(void);
uint64_t apic_ticks_to_ns(uint64_t ticks);
void apic_calibrate_post_irq(void);

#ifdef __cplusplus
}
#endif

#endif
