#ifndef AXIOME_IOAPIC_H
#define AXIOME_IOAPIC_H

#include <stdint.h>

void ioapic_init(void);
void ioapic_mask(unsigned int gsi, int mask);
void ioapic_set_entry(unsigned int gsi, uint8_t vector, uint8_t dest,
                      int masked);

#endif
