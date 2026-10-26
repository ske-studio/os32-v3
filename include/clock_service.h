/* Cooperative clock service. Register a complete resident table at boot. */
#ifndef OS32_CLOCK_SERVICE_H
#define OS32_CLOCK_SERVICE_H
#include "types.h"

/* Legacy KAPI layout: seven bytes, with a two-digit year. */
typedef struct {
    u8 year, month, day, wday, hour, min, sec;
} RTC_Time;

/* Internal calendar date with a full Gregorian year. */
typedef struct {
    int year;
    u8 month, day, wday, hour, min, sec;
} ClockDate;

typedef struct ClockOps {
    void (*read_date)(ClockDate *date);
    void (*read_legacy)(RTC_Time *time);
} ClockOps;

void clock_register(const ClockOps *ops);
void clock_read_date(ClockDate *date);
void clock_read_legacy(RTC_Time *time);
#endif
