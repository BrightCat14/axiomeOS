#ifndef AXIOME_CLOCK_H
#define AXIOME_CLOCK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise the kernel wall-clock.  Must be called after hal_cpu_irq_enable()
   so that apic_get_ticks() is already incrementing.
   boot_unix    : Unix timestamp (UTC seconds since epoch) at boot.
   tz_offset_sec: seconds east of UTC (e.g. +10800 for UTC+3). */
void clock_init(uint64_t boot_unix, int64_t tz_offset_sec);

/* Monotonic nanoseconds since clock_init().  Never goes backwards. */
uint64_t clock_mono_ns(void);

/* Wall-clock seconds (local time = UTC + tz_offset). */
uint64_t clock_wall_sec(void);

/* Wall-clock with sub-second precision. */
void clock_wall_ns(uint64_t *sec_out, uint64_t *nsec_out);

#ifdef __cplusplus
}
#endif

#endif
