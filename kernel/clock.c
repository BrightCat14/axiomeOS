#include "clock.h"
#include "apic.h"

/* Snapshot taken at clock_init() time. */
static uint64_t boot_tick;       /* apic_get_ticks() value at init        */
static uint64_t boot_unix_sec;   /* wall-clock epoch at init (UTC + tz)   */

void clock_init(uint64_t boot_unix, int64_t tz_offset_sec)
{
    boot_unix_sec = (uint64_t)((int64_t)boot_unix + tz_offset_sec);
    boot_tick     = apic_get_ticks();
}

/* Nanoseconds elapsed since clock_init(). */
uint64_t clock_mono_ns(void)
{
    uint64_t elapsed = apic_get_ticks() - boot_tick;
    return apic_ticks_to_ns(elapsed);
}

uint64_t clock_wall_sec(void)
{
    return boot_unix_sec + clock_mono_ns() / 1000000000ULL;
}

void clock_wall_ns(uint64_t *sec_out, uint64_t *nsec_out)
{
    uint64_t mono = clock_mono_ns();
    uint64_t s    = boot_unix_sec + mono / 1000000000ULL;
    uint64_t ns   = mono % 1000000000ULL;
    if (sec_out)  *sec_out  = s;
    if (nsec_out) *nsec_out = ns;
}
