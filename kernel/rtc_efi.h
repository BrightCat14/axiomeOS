#ifndef AXIOME_RTC_EFI_H
#define AXIOME_RTC_EFI_H

#include <stdint.h>

/* Save the EFI system table physical address from the MB2 tag.
   Called from mb2_parse() — does nothing except store the address. */
void rtc_efi_set_systable(uint64_t systable_phys);

/* Map EFI pages and call GetTime() to read the RTC.
   Must be called after isr_init() and vmm_init() so that:
     - exceptions are handled (IDT live)
     - vmm_mmap_phys() is available to map the EFI pages
   Caches the result; safe to call rtc_efi_get_unix() any time after.  */
void rtc_efi_init(void);

/* Returns the cached Unix UTC timestamp, or 0 if unavailable. */
uint64_t rtc_efi_get_unix(void);

#endif
