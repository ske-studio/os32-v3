/* RTC hardware result, fed through the real PC98 adapter and service. */
#include "../../drivers/rtc.h"
#include "../../kernel/clock_service.c"
#include "os_time.h"
#include "../../lib/os_time.c"
static RTC_Time clock_sample;
static int clock_reads;
void rtc_read(RTC_Time *time) { *time = clock_sample; clock_reads++; }
#include "clock_adapter.inc"
#include "clock_consumers.inc"
_Static_assert(sizeof(RTC_Time) == 7, "RTC legacy ABI size");
#define RTC_OFFSET(field, value) _Static_assert(__builtin_offsetof(RTC_Time, field) == value, "RTC " #field)
RTC_OFFSET(year, 0); RTC_OFFSET(month, 1); RTC_OFFSET(day, 2);
RTC_OFFSET(wday, 3); RTC_OFFSET(hour, 4); RTC_OFFSET(min, 5); RTC_OFFSET(sec, 6);
#include "clock_wrapper.inc"
static int clock_check(void)
{
    static const u8 years[] = {79, 80, 0, 99};
    static const int full_years[] = {2079, 1980, 2000, 1999};
    /* Independent golden FAT words from the old year/pivot/bit contract. */
    static const u32 fat_times[] = {0xc65dbf7d, 0x005dbf7d, 0x285dbf7d, 0x265dbf7d};
    ClockDate date;
    RTC_Time legacy;
    for (unsigned int i = 0; i < sizeof(years); i++) {
        clock_sample = (RTC_Time){years[i], 2, 29, 4, 23, 59, 59};
        clock_reads = 0;
        clock_read_date(&date);
        CHECK(clock_reads == 1 && date.year == full_years[i]);
        CHECK(date.month == 2 && date.day == 29 && date.wday == 4);
        CHECK(date.hour == 23 && date.min == 59 && date.sec == 59);
        CHECK(get_fattime() == fat_times[i] && clock_reads == 2);
        CHECK(sys_time() == datetime_to_epoch(full_years[i], 2, 29, 23, 59, 59));
        CHECK(clock_reads == 3);
        wrap_rtc_read(&legacy);
        CHECK(clock_reads == 4 && hit == CLOCK_SLOT);
        CHECK(legacy.year == years[i] && legacy.month == 2 && legacy.day == 29);
        CHECK(legacy.wday == 4 && legacy.hour == 23 && legacy.min == 59 && legacy.sec == 59);
    }
    CHECK(datetime_to_epoch(2000, 3, 1, 0, 0, 0) -
          datetime_to_epoch(2000, 2, 29, 0, 0, 0) == 86400);
    return 0;
}
