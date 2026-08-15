#include "test_util.h"

#include <stddef.h>

#include "time.h"

static int test_libc_time(void)
{
    /* localtime/gmtime around the epoch. */
    {
        time_t t = 0;
        struct tm *tm = localtime(&t);
        CHECK(tm != NULL);
        CHECK_EQ(tm->tm_year, 70);
        CHECK_EQ(tm->tm_mon, 0);
        CHECK_EQ(tm->tm_mday, 1);
        CHECK_EQ(tm->tm_hour, 0);
        CHECK_EQ(tm->tm_min, 0);
        CHECK_EQ(tm->tm_sec, 0);
        CHECK_EQ(tm->tm_wday, 4); /* 1970-01-01 was a Thursday */
        CHECK_EQ(tm->tm_yday, 0);
    }

    /* Non-leap year. */
    {
        time_t t = 1577836800; /* 2020-01-01 00:00:00 UTC */
        struct tm *tm = gmtime(&t);
        CHECK(tm != NULL);
        CHECK_EQ(tm->tm_year, 120);
        CHECK_EQ(tm->tm_mon, 0);
        CHECK_EQ(tm->tm_mday, 1);
        CHECK_EQ(tm->tm_wday, 3); /* Wednesday */
    }

    /* Leap year: 2000-02-29. */
    {
        time_t t = 951782400; /* 2000-02-29 00:00:00 UTC */
        struct tm *tm = gmtime(&t);
        CHECK(tm != NULL);
        CHECK_EQ(tm->tm_year, 100);
        CHECK_EQ(tm->tm_mon, 1);
        CHECK_EQ(tm->tm_mday, 29);
        CHECK_EQ(tm->tm_wday, 2); /* Tuesday */
    }

    /* End of the millenium: 1999-12-31 23:59:59. */
    {
        time_t t = 946684799;
        struct tm *tm = gmtime(&t);
        CHECK(tm != NULL);
        CHECK_EQ(tm->tm_year, 99);
        CHECK_EQ(tm->tm_mon, 11);
        CHECK_EQ(tm->tm_mday, 31);
        CHECK_EQ(tm->tm_hour, 23);
        CHECK_EQ(tm->tm_min, 59);
        CHECK_EQ(tm->tm_sec, 59);
        CHECK_EQ(tm->tm_yday, 364);
    }

    /* Day-of-month rollover at month end. */
    {
        time_t t = 1583107200; /* 2020-03-02 00:00:00 UTC (2020 is leap) */
        struct tm *tm = gmtime(&t);
        CHECK(tm != NULL);
        CHECK_EQ(tm->tm_year, 120);
        CHECK_EQ(tm->tm_mon, 2);
        CHECK_EQ(tm->tm_mday, 2);
        CHECK_EQ(tm->tm_yday, 61);
    }

    /* localtime returns NULL for a NULL argument. */
    CHECK(localtime(NULL) == NULL);

    /* asctime formatting. */
    {
        struct tm tm = {0};
        tm.tm_year = 70;
        tm.tm_mon = 0;
        tm.tm_mday = 1;
        tm.tm_hour = 0;
        tm.tm_min = 0;
        tm.tm_sec = 0;
        tm.tm_wday = 4;
        CHECK_STR(asctime(&tm), "Thu Jan  1 00:00:00 1970\n");
        CHECK(asctime(NULL) == NULL);
    }

    /* ctime == asctime(localtime(t)). */
    {
        time_t t = 1577836800;
        CHECK_STR(ctime(&t), "Wed Jan  1 00:00:00 2020\n");
    }

    TEST_REPORT("libc/time");
}

int main(void)
{
    return test_libc_time();
}
