#include "rtc_efi.h"
#include "printk.h"

/* ---- CMOS RTC via ports 0x70 / 0x71 --------------------------------------
   Works on all x86 hardware and QEMU regardless of UEFI/BIOS boot mode.
   No memory mapping required; safe to call any time after early boot.       */

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

/* RTC register indices */
#define RTC_SECONDS   0x00
#define RTC_MINUTES   0x02
#define RTC_HOURS     0x04
#define RTC_WEEKDAY   0x06
#define RTC_DAY       0x07
#define RTC_MONTH     0x08
#define RTC_YEAR      0x09
#define RTC_CENTURY   0x32
#define RTC_STATUS_A  0x0A
#define RTC_STATUS_B  0x0B

#define RTC_UIP       0x80   /* Update In Progress bit in Status A */
#define RTC_24H       0x02   /* 24-hour mode bit in Status B       */
#define RTC_BCD       0x04   /* BCD mode bit in Status B (active low: 0=BCD) */

static uint64_t g_unix_time = 0;

static uint8_t cmos_read(uint8_t reg)
{
    __asm__ volatile("outb %0, %1" : : "a"(reg), "d"((uint16_t)CMOS_ADDR));
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "d"((uint16_t)CMOS_DATA));
    return v;
}

static uint8_t bcd_to_bin(uint8_t bcd)
{
    return (uint8_t)(((bcd >> 4) & 0x0F) * 10 + (bcd & 0x0F));
}

static uint64_t tm_to_unix(int year, int mon, int day,
                            int hour, int min, int sec)
{
    static const int mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int y = year - 1970;
    uint64_t days = (uint64_t)y * 365
                  + (uint64_t)((y + 1) / 4)
                  - (uint64_t)((y + 69) / 100)
                  + (uint64_t)((y + 369) / 400);
    int is_leap = ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0);
    for (int m = 1; m < mon; m++) {
        days += mdays[m - 1];
        if (m == 2 && is_leap) days++;
    }
    days += (uint64_t)(day - 1);
    return days * 86400ULL
         + (uint64_t)hour * 3600ULL
         + (uint64_t)min  * 60ULL
         + (uint64_t)sec;
}

/* Stub: EFI system table no longer used — CMOS RTC is read directly. */
void rtc_efi_set_systable(uint64_t systable_phys)
{
    (void)systable_phys;
}

void rtc_efi_init(void)
{
    /* Wait for any in-progress RTC update to finish. */
    while (cmos_read(RTC_STATUS_A) & RTC_UIP)
        ;

    /* Read all fields. */
    uint8_t sec  = cmos_read(RTC_SECONDS);
    uint8_t min  = cmos_read(RTC_MINUTES);
    uint8_t hour = cmos_read(RTC_HOURS);
    uint8_t day  = cmos_read(RTC_DAY);
    uint8_t mon  = cmos_read(RTC_MONTH);
    uint8_t year = cmos_read(RTC_YEAR);
    uint8_t cent = cmos_read(RTC_CENTURY);
    uint8_t statb = cmos_read(RTC_STATUS_B);

    /* Wait again and re-read to confirm no update occurred mid-read. */
    while (cmos_read(RTC_STATUS_A) & RTC_UIP)
        ;
    uint8_t sec2  = cmos_read(RTC_SECONDS);
    uint8_t min2  = cmos_read(RTC_MINUTES);
    uint8_t hour2 = cmos_read(RTC_HOURS);
    uint8_t day2  = cmos_read(RTC_DAY);
    uint8_t mon2  = cmos_read(RTC_MONTH);
    uint8_t year2 = cmos_read(RTC_YEAR);
    uint8_t cent2 = cmos_read(RTC_CENTURY);

    /* Use second read if it differs (update happened between reads). */
    if (sec2 != sec || min2 != min) {
        sec = sec2; min = min2; hour = hour2;
        day = day2; mon = mon2; year = year2; cent = cent2;
    }

    /* Convert BCD to binary if needed (Status B bit 2 = 0 means BCD). */
    if (!(statb & RTC_BCD)) {
        sec  = bcd_to_bin(sec);
        min  = bcd_to_bin(min);
        hour = bcd_to_bin(hour & 0x7F);  /* mask AM/PM bit */
        day  = bcd_to_bin(day);
        mon  = bcd_to_bin(mon);
        year = bcd_to_bin(year);
        cent = bcd_to_bin(cent);
    }

    /* Handle 12-hour mode (Status B bit 1 = 0 means 12h). */
    if (!(statb & RTC_24H) && (hour & 0x80)) {
        hour = (uint8_t)((hour & 0x7F) + 12);
        if (hour == 24) hour = 12;
    }

    /* Full 4-digit year. */
    int full_year;
    if (cent != 0)
        full_year = (int)cent * 100 + (int)year;
    else
        full_year = (year >= 70) ? (1900 + (int)year) : (2000 + (int)year);

    printk("RTC: %04d-%02u-%02u %02u:%02u:%02u (UTC)\n",
           full_year, mon, day, hour, min, sec);

    /* Sanity check. */
    if (full_year < 2020 || full_year > 2100 ||
        mon < 1 || mon > 12 || day < 1 || day > 31 ||
        hour > 23 || min > 59 || sec > 59) {
        printk("RTC: implausible values, ignoring\n");
        return;
    }

    g_unix_time = tm_to_unix(full_year, (int)mon, (int)day,
                             (int)hour, (int)min, (int)sec);
    printk("RTC: boot epoch=%lu\n", (unsigned long)g_unix_time);
}

uint64_t rtc_efi_get_unix(void)
{
    return g_unix_time;
}
