#include "os32api.h"
#include <stdio.h>

int main(int argc, char **argv, KernelAPI *api)
{
    RTC_Time_Ext time = { .year = 255, .month = 255, .day = 255, .wday = 255,
                          .hour = 255, .min = 255, .sec = 255 };
    char line[48];

    (void)argc;
    (void)argv;
    api->rtc_read(&time);
    snprintf(line, sizeof(line), "RTC %u %u %u %u %u %u %u\n",
             (unsigned)time.year, (unsigned)time.month, (unsigned)time.day,
             (unsigned)time.wday, (unsigned)time.hour, (unsigned)time.min,
             (unsigned)time.sec);
    for (const char *p = line; *p; ++p)
        api->shell_putchar(*p, 7);
    return 0;
}
