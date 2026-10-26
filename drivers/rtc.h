/* ======================================================================== */
/*  RTC.H — µPD4990A カレンダ時計ドライバ                                   */
/*                                                                          */
/*  PC-9801内蔵RTC (µPD4990A) をI/Oポート直接制御で操作                      */
/*                                                                          */
/*  ■ ポート0x20 ビットレイアウト (PC9800Bible §4-3):                       */
/*    D7 D6 D5  D4  D3  D2 D1 D0                                           */
/*     x  x  DI CLK STB C2 C1 C0                                           */
/*                                                                          */
/*  出典: PC9800Bible §2-4, §4-3                                           */
/* ======================================================================== */

#ifndef __RTC_H
#define __RTC_H

#include "../include/clock_service.h"
#include "pc98.h"

/* 00-79 -> 2000-2079; 80-99 -> 1980-1999. */
#define RTC_YEAR_PIVOT 80
#define RTC_YEAR_BASE_NEW 2000
#define RTC_YEAR_BASE_OLD 1900

#define RTC_MODE_MASK 0x07
#define RTC_NIBBLE_BITS 4
#define RTC_NIBBLE_COUNT (RTC_READ_BITS / RTC_NIBBLE_BITS)
#define RTC_NIB_SEC_ONES 0
#define RTC_NIB_SEC_TENS 1
#define RTC_NIB_MIN_ONES 2
#define RTC_NIB_MIN_TENS 3
#define RTC_NIB_HOUR_ONES 4
#define RTC_NIB_HOUR_TENS 5
#define RTC_NIB_DAY_ONES 6
#define RTC_NIB_DAY_TENS 7
#define RTC_NIB_WDAY 8
#define RTC_NIB_MONTH 9
#define RTC_NIB_YEAR_ONES 10
#define RTC_NIB_YEAR_TENS 11
#define RTC_SECMIN_TENS_MASK 0x07
#define RTC_HOURDAY_TENS_MASK 0x03
#define RTC_WDAY_MASK 0x07
#define RTC_DIGIT_MASK 0x0F
#define RTC_SECMIN_MAX 59

/* ======== I/Oポート定義 ======== */
#define RTC_READ   SYSPORT_B    /* ポートB読み出し (bit0 = CDAT = DATA OUT) */

/* ======== ポート0x20 ビット配置 (PC9800Bible §4-3) ======== */
/*   bit5: DI  (Data Input — シフトレジスタへのデータ入力)  */
/*   bit4: CLK (Clock — シフトクロック)                     */
/*   bit3: STB (Strobe — 0→1でモードセット/コマンドラッチ)  */
/*   bit2-0: C2-C0 (ファンクションモード選択)               */
#define RTC_DI     0x20    /* bit5: データ入力 */
#define RTC_CLK    0x10    /* bit4: クロック */
#define RTC_STB    0x08    /* bit3: ストローブ */

/* ======== ファンクションモード (C2 C1 C0) ======== */
/* ポート0x20のbit0-2に直接書き込む                  */
#define RTC_MODE_HOLD       0x00   /* 000: レジスタホールド (DATA OUT=1Hz) */
#define RTC_MODE_SHIFT      0x01   /* 001: レジスタシフト (DATA OUT=SR LSB) */
#define RTC_MODE_TIMESET    0x02   /* 010: タイムセット/カウンタホールド */
#define RTC_MODE_TIMEREAD   0x03   /* 011: タイムリード (DATA OUT=0.5Hz) */

#define RTC_READ_BITS      48     /* RTCから読み出すビット数 */

/* ======== API ======== */

extern const ClockOps pc98_clock;

/* RTC初期化 (レジスタホールドモードに設定) */
void rtc_init(void);

/* 現在日時を読み出す */
void rtc_read(RTC_Time *t);

/* uptimeをtick_countから計算し秒で返す */
u32  rtc_uptime_sec(void);

#endif /* __RTC_H */
