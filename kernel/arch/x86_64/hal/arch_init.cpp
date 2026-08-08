#include "x86_hal.h"

/* Architecture entry point for the HAL: construct every x86_64 backend into
   its static storage and bind it into hal::platform(). */

extern "C" void hal_arch_init(void)
{
    hal::HalPlatform &plat = hal::platform();

    plat.port_io = hal::x86_64::x86_portio_create();
    plat.cpu     = hal::x86_64::x86_cpu_create();
    plat.irq     = hal::x86_64::x86_irq_create();
    plat.timer   = hal::x86_64::x86_timer_create();
    plat.serial  = hal::x86_64::x86_serial_create();
    plat.mmio    = hal::x86_64::x86_mmio_create();
}
