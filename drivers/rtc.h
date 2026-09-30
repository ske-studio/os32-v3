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

#include "types.h"

/* ======== I/Oポート定義 ======== */
#define RTC_SET    0x20    /* コマンド/データ書込みポート */
#define RTC_READ   0x33    /* ポートB読み出し (bit0 = CDAT = DATA OUT) */

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

/* ======== 時刻構造体 ======== */
typedef struct {
    u8 year;     /* 00-99 (BCD → 変換済み10進) */
    u8 month;    /* 1-12 */
    u8 day;      /* 1-31 */
    u8 wday;     /* 0=日, 1=月, ..., 6=土 */
    u8 hour;     /* 0-23 */
    u8 min;      /* 0-59 */
    u8 sec;      /* 0-59 */
} RTC_Time;

/* ======== API ======== */

/* RTC初期化 (レジスタホールドモードに設定) */
void rtc_init(void);

/* 現在日時を読み出す */
void rtc_read(RTC_Time *t);

/* uptimeをtick_countから計算し秒で返す */
u32  rtc_uptime_sec(void);

#endif /* __RTC_H */
