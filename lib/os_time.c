#include "os_time.h"

/* Calendar and unit constants local until the shared time header adopts them. */
#define OS_TIME_EPOCH_YEAR 1970
#define DAYS_PER_YEAR 365
#define DAYS_PER_LEAP_YEAR 366
#define MONTHS_PER_YEAR 12
#define HOURS_PER_DAY 24
#define MINS_PER_HOUR 60
#define SECS_PER_MIN 60
#define SEC_PER_HOUR (SECS_PER_MIN * MINS_PER_HOUR * 1UL)
#define SEC_PER_DAY (SEC_PER_HOUR * HOURS_PER_DAY)

static int is_leap_year(int y) {
    if (y % 400 == 0) return 1;
    if (y % 100 == 0) return 0;
    if (y % 4 == 0) return 1;
    return 0;
}

static const int days_in_month[2][12] = {
    {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
    {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}
};

os_time_t datetime_to_epoch(int year, int month, int day, int hour, int min, int sec) {
    int y, m, i;
    u32 days = 0;
    os_time_t epoch;
    int leap;

    /* 範囲外の値をクランプする。RTC の読み損ないや壊れた FAT の
     * タイムスタンプで負値が来ると、u32 演算に混ざって巨大な
     * epoch に化ける (符号昇格)。1970 未満は epoch 0 相当に丸める。 */
    if (year < OS_TIME_EPOCH_YEAR) year = OS_TIME_EPOCH_YEAR;
    if (hour < 0) hour = 0; else if (hour > HOURS_PER_DAY - 1) hour = HOURS_PER_DAY - 1;
    if (min  < 0) min  = 0; else if (min  > MINS_PER_HOUR - 1) min  = MINS_PER_HOUR - 1;
    if (sec  < 0) sec  = 0; else if (sec  > SECS_PER_MIN - 1) sec  = SECS_PER_MIN - 1;

    /* 1970年からの経過日数を計算 */
    for (y = OS_TIME_EPOCH_YEAR; y < year; y++) {
        days += is_leap_year(y) ? DAYS_PER_LEAP_YEAR : DAYS_PER_YEAR;
    }
    
    /* 年内の経過日数を計算 */
    m = (month < 1) ? 1 : ((month > MONTHS_PER_YEAR) ? MONTHS_PER_YEAR : month);
    leap = is_leap_year(year);
    for (i = 0; i < m - 1; i++) {
        days += days_in_month[leap][i];
    }
    
    days += (day > 0 ? day - 1 : 0);
    
    /* 日数を秒に変換し、時間・分・秒を足す */
    epoch = days * SEC_PER_DAY;
    epoch += hour * SEC_PER_HOUR;
    epoch += min * (SECS_PER_MIN * 1UL);
    epoch += sec;
    
    return epoch;
}

os_time_t dos_time_to_epoch(u16 dos_date, u16 dos_time) {
    /* dos_date: 0-4=day, 5-8=month, 9-15=year-1980 */
    int day   = (dos_date & 0x001F);
    int month = ((dos_date >> 5) & 0x000F);
    int year  = ((dos_date >> 9) & 0x007F) + 1980;
    
    /* dos_time: 0-4=sec/2, 5-10=min, 11-15=hour */
    int sec   = (dos_time & 0x001F) * 2;
    int min   = ((dos_time >> 5) & 0x003F);
    int hour  = ((dos_time >> 11) & 0x001F);
    
    return datetime_to_epoch(year, month, day, hour, min, sec);
}
