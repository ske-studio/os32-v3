#include "platform_sys.h"
#include "sys/arch/ppc/kernel_boot.h"
#include "sys/dev/macintosh/via.h"
#include "io.h"
#include "clock_service.h"
#include "kboot_info.h"
#include "kprintf.h"
#include "via_window.h"

static struct via_controller controller;
static int boot_error = PM_VIA_NOT_READY;

static u8 via_read(void *cookie, u32 offset)
{
    u8 value = ((volatile u8 *)cookie)[offset];
    __asm__ volatile("eieio" ::: "memory");
    return value;
}

static void via_write(void *cookie, u32 offset, u8 value)
{
    ((volatile u8 *)cookie)[offset] = value;
    __asm__ volatile("eieio" ::: "memory");
}

static unsigned long long via_tb(void *cookie)
{
    (void)cookie;
    u32 hi, lo, again;
    do {
        __asm__ volatile("mftbu %0" : "=r"(hi));
        __asm__ volatile("mftb %0" : "=r"(lo));
        __asm__ volatile("mftbu %0" : "=r"(again));
    } while (hi != again);
    return ((unsigned long long)hi << 32) | lo;
}

#if KBOOT_SH
enum {
    MAC_RTC_EPOCH_YEAR = 1904, MAC_RTC_EPOCH_WEEKDAY = 5, /* Friday */
    RTC_SECS_PER_MIN = 60, RTC_SECS_PER_HOUR = 3600, RTC_SECS_PER_DAY = 86400
};

static int pm_rtc_leap_year(int year)
{
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

static void pm_rtc_date(ClockDate *date)
{
    static const u8 month_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    u32 seconds;
    *date = (ClockDate){0};
    if (via_read_rtc(&controller, &seconds))
        return;

    u32 days = seconds / RTC_SECS_PER_DAY;
    u32 day_seconds = seconds % RTC_SECS_PER_DAY;
    date->wday = (days + MAC_RTC_EPOCH_WEEKDAY) % 7;
    int year = MAC_RTC_EPOCH_YEAR;
    while (days >= 365u + pm_rtc_leap_year(year)) {
        days -= 365u + pm_rtc_leap_year(year);
        ++year;
    }
    unsigned month = 0;
    for (; month < 11; ++month) {
        unsigned length = month_days[month] + (month == 1 && pm_rtc_leap_year(year));
        if (days < length)
            break;
        days -= length;
    }
    date->year = year;
    date->month = month + 1;
    date->day = days + 1;
    date->hour = day_seconds / RTC_SECS_PER_HOUR;
    date->min = day_seconds / RTC_SECS_PER_MIN % 60;
    date->sec = day_seconds % RTC_SECS_PER_MIN;
}

static void pm_rtc_legacy(RTC_Time *time)
{
    ClockDate date;
    pm_rtc_date(&date);
    *time = (RTC_Time){
        .year = date.year % 100, .month = date.month, .day = date.day,
        .wday = date.wday, .hour = date.hour, .min = date.min, .sec = date.sec,
    };
}

static const ClockOps pm_rtc_clock = {
    .read_date = pm_rtc_date, .read_legacy = pm_rtc_legacy,
};
#endif

void pm_via_prepare(void)
{
    const struct pm_kboot_info *info = ppc_boot_info();
    const struct pm_via_window *window = pm_via_window();
    enum via_kind kind = info->via.kind == PM_VIA_PMU ? VIA_PMU :
                         info->via.kind == PM_VIA_CUDA ? VIA_CUDA : VIA_NONE;
    const char *name = kind == VIA_PMU ? "pmu" : kind == VIA_CUDA ? "cuda" : "none";
    controller = (struct via_controller){
        .kind = kind,
        .io = { (void *)window->va, via_read, via_write, via_tb, info->tb_hz },
    };
    boot_error = window->rc;
    if (!boot_error) boot_error = via_prepare(&controller);
#if KBOOT_SH
    if (!boot_error)
        clock_register(&pm_rtc_clock);
#endif
    if (boot_error)
        kprintf(7, "VIA kind=%s pa=%08x va=%08x rc=%d compatible=%s\r\n",
                name, info->via.pa, window->va, boot_error, info->via.compatible);
    else
        kprintf(7, "VIA kind=%s pa=%08x va=%08x rc=ok compatible=%s\r\n",
                name, info->via.pa, window->va, info->via.compatible);
}

void platform_reset(void)
{
    _disable();
    int rc = boot_error ? boot_error : via_restart(&controller);
    if (!rc) return;
    kprintf(7, "VIA reset rc=%d\r\n", rc);
    unsigned long long start = via_tb(0), now = start;
    while (now - start < 2ULL * controller.io.tb_hz) {
        pm_lan_poll(now);
        now = via_tb(0);
    }
}
