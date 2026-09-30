/* ======================================================================== */
/*  CAL.C -- カレンダー表示                                                   */
/*                                                                          */
/*  Usage: cal [MONTH YEAR]                                                  */
/*  引数なしで当月、引数ありで指定月のカレンダーを表示する。                     */
/* ======================================================================== */
#include "os32api.h"
#include <stdio.h>

static KernelAPI *api;

/* 各月の日数 (うるう年でない場合) */
static const int days_in_month[] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

static int is_leap(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static int month_days(int month, int year)
{
    if (month == 2 && is_leap(year)) return 29;
    return days_in_month[month];
}

/* ツェラーの公式 (グレゴリオ暦) で曜日を求める
   戻り値: 0=日, 1=月, ..., 6=土 */
static int day_of_week(int year, int month, int day)
{
    int q, m, k, j, h;
    if (month < 3) {
        month += 12;
        year--;
    }
    q = day;
    m = month;
    k = year % 100;
    j = year / 100;
    h = (q + (13 * (m + 1)) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;
    /* h: 0=土, 1=日, 2=月, ..., 6=金 → 0=日, 1=月, ... に変換 */
    return ((h + 6) % 7);
}

static int parse_int(const char *s)
{
    int val = 0;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    return val;
}

int main(int argc, char **argv, KernelAPI *kapi)
{
    int month, year;
    int start_dow, ndays;
    int row, col, day;

    api = kapi;

    if (argc >= 3) {
        month = parse_int(argv[1]);
        year = parse_int(argv[2]);
    } else {
        /* RTC から現在の年月を取得 */
        RTC_Time_Ext rtc;
        api->rtc_read(&rtc);
        year = 2000 + rtc.year; /* RTCは下2桁 */
        month = rtc.month;
    }

    if (month < 1 || month > 12 || year < 1) {
        printf("Usage: cal [MONTH YEAR]\n");
        return 1;
    }

    ndays = month_days(month, year);
    start_dow = day_of_week(year, month, 1);

    /* ヘッダー */
    printf("     %4d/%02d\n", year, month);
    printf(" Su Mo Tu We Th Fr Sa\n");

    /* 先頭の空白 */
    col = 0;
    for (col = 0; col < start_dow; col++) {
        printf("   ");
    }

    /* 日付 */
    for (day = 1; day <= ndays; day++) {
        printf("%3d", day);
        col++;
        if (col == 7) {
            printf("\n");
            col = 0;
        }
    }
    if (col != 0) printf("\n");

    return 0;
}
