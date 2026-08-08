#ifndef AXIOME_HAL_H
#define AXIOME_HAL_H

#include <stdint.h>
#include <stddef.h>

#include "hal_portio.h"
#include "hal_cpu.h"
#include "hal_irq.h"
#include "hal_timer.h"
#include "hal_serial.h"
#include "hal_mmio.h"

/* Minimal placement new so the architecture backends can be constructed
   into static storage without pulling in any libstdc++ runtime or heap.
   The sized delete is never called (nothing is destroyed) but the compiler
   may reference it from virtual destructors. */
inline void *operator new(size_t, void *ptr) noexcept { return ptr; }
inline void  operator delete(void *, void *) noexcept {}
inline void  operator delete(void *, size_t) noexcept {}

namespace hal {

/* Global binding of the abstract interfaces to one concrete architecture.
   Populated by hal_arch_init(), which every port must provide. */
struct HalPlatform
{
    IPortIo        *port_io;
    ICpu           *cpu;
    IIrqController *irq;
    ITimer         *timer;
    ISerial        *serial;
    IMmio          *mmio;
};

HalPlatform &platform(void);

inline IPortIo        &port_io() { return *platform().port_io; }
inline ICpu           &cpu()     { return *platform().cpu; }
inline IIrqController &irq()     { return *platform().irq; }
inline ITimer         &timer()   { return *platform().timer; }
inline ISerial        &serial()  { return *platform().serial; }
inline IMmio          &mmio()    { return *platform().mmio; }

} /* namespace hal */

#ifdef __cplusplus
extern "C" {
#endif

/* Bootstrap the HAL: construct the architecture backends. Must be called
   before any other hal_* function. */
void hal_init(void);

/* Provided by the architecture (kernel/arch/<arch>/hal/arch_init.cpp). */
void hal_arch_init(void);

#ifdef __cplusplus
}
#endif

#endif
