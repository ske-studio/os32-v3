#ifndef OS_TIME_H
#define OS_TIME_H

#include "os32_kapi_shared.h"

/* 日時から UNIX Epoch への変換 */
/* 注意 (既知の制約): os_time_t は符号なし 32bit の UNIX epoch 秒。
 * 2106-02-07 06:28:15 UTC で一周する (いわゆる Y2106 問題)。
 * PC-98 の RTC は 2 桁年なので実用上そこまで届かないが、FAT の
 * タイムスタンプから未来日付を食わせると折り返す。64bit 化が要る場合は
 * os_time_t の定義とこの API 一式を同時に変えること。 */
os_time_t datetime_to_epoch(int year, int month, int day, int hour, int min, int sec);

/* MS-DOS フォーマットの Date と Time から UNIX Epoch への変換 */
os_time_t dos_time_to_epoch(u16 dos_date, u16 dos_time);

#endif /* OS_TIME_H */
