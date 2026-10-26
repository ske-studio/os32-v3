/* Calendar conversion and RTC I/O stay in the provider. */
#include "clock_service.h"

static void clock_empty_date(ClockDate *date)
{
    *date = (ClockDate){0};
}
static void clock_empty_legacy(RTC_Time *time)
{
    *time = (RTC_Time){0};
}
static const ClockOps clock_empty = {
    .read_date = clock_empty_date, .read_legacy = clock_empty_legacy,
};
static const ClockOps *clock_provider = &clock_empty;

void clock_register(const ClockOps *ops) { clock_provider = ops; }
void clock_read_date(ClockDate *date) { clock_provider->read_date(date); }
void clock_read_legacy(RTC_Time *time) { clock_provider->read_legacy(time); }
