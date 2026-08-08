#ifndef AXIOME_X86_64_HAL_H
#define AXIOME_X86_64_HAL_H

#include "../../../hal/hal.h"

namespace hal {
namespace x86_64 {

/* Each factory constructs a concrete backend into static storage owned by
   its translation unit and returns the abstract interface pointer. Called
   once from hal_arch_init(). */
IPortIo        *x86_portio_create(void);
ICpu           *x86_cpu_create(void);
IIrqController *x86_irq_create(void);
ITimer         *x86_timer_create(void);
ISerial        *x86_serial_create(void);
IMmio          *x86_mmio_create(void);

} /* namespace x86_64 */
} /* namespace hal */

#endif
