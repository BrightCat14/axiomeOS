#ifndef AXIOME_IOAPIC_H
#define AXIOME_IOAPIC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void ioapic_init(void);
void ioapic_mask(unsigned int gsi, int mask);
void ioapic_set_entry(unsigned int gsi, uint8_t vector, uint8_t dest,
                      int masked);
void ioapic_set_entry_flags(unsigned int gsi, uint8_t vector, uint8_t dest,
                            int masked, uint16_t flags);

#ifdef __cplusplus
}
#endif

#endif
