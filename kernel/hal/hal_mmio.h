#ifndef AXIOME_HAL_MMIO_H
#define AXIOME_HAL_MMIO_H

#include <stdint.h>
#include <stddef.h>

namespace hal {

/* Physical memory-mapped I/O. On x86_64 this maps the 2 MiB window that
   contains `phys` into the higher half and returns its (identity) virtual
   address. On other architectures this maps through the platform MMU as
   appropriate. */
class IMmio
{
public:
    virtual void *map_phys(uint64_t phys, size_t size) = 0;
    virtual ~IMmio() = default;
};

} /* namespace hal */

#endif
