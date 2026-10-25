/* ======================================================================== */
/*  RTC.C — µPD4990A カレンダ時計ドライバ                                   */
/*                                                                          */
/*  PC-98内蔵RTC (µPD4990A) をI/Oポート直接制御で操作                        */
/*                                                                          */
/*  ■ ポート0x20 ビットレイアウト (PC9800Bible §4-3):                       */
/*    D7 D6 D5  D4  D3  D2 D1 D0                                           */
/*     x  x  DI CLK STB C2 C1 C0                                           */
/*                                                                          */
/*  ■ ファンクションモード (C2 C1 C0):                                      */
/*    000 = レジスタホールド     (DATA OUT = 1Hz)                            */
/*    001 = レジスタシフト       (DATA OUT = シフトレジスタLSB) ★読み出し用 */
/*    010 = タイムセット/カウンタホールド                                    */
/*    011 = タイムリード         (DATA OUT = 0.5Hz) ★カウンタ→SR転送       */
/*                                                                          */
/*  ■ 読み出し手順:                                                         */
/*    1. タイムリード (011) → カウンタの現在値をシフトレジスタにコピー       */
/*    2. レジスタシフト (001) → DATA OUTからシフトレジスタ内容を読み出し     */
/*    3. 48ビット (12ニブル) を CLK パルスで1ビットずつ読み出す              */
/*                                                                          */
/*  ■ データ形式 (LSBファースト, 各4ビット):                                */
/*    1秒, 10秒, 1分, 10分, 1時, 10時, 1日, 10日, 曜日, 月, 1年, 10年       */
/*                                                                          */
/*  出典: PC9800Bible §2-4, §4-3                                           */
/* ======================================================================== */

#include "rtc.h"
#include "io.h"
#include "memmap.h"

/* 外部: tick_count (idt.c) */
extern volatile u32 tick_count;

/* ======== I/Oウェイト ======== */
/* io_wait()×2回で約1.2µsの遅延 (µPD4990A最小CLKパルス幅: 0.9µs) */
static void rtc_wait(void)
{
    io_wait();
    io_wait();
}

/* ======== コマンド設定 (パラレル方式) ======== */
/*                                                                          */
/* PC-98のµPD4990AはC0-C2をポート0x20のbit0-2に直接書き込むパラレル方式。    */
/* STBの立ち上がり (0→1) でコマンドがラッチされる。                         */
/*                                                                          */
/* 手順:                                                                    */
/*   1. C2C1C0 + STB=0 を出力 (コマンドビット準備)                          */
/*   2. C2C1C0 + STB=1 を出力 (ストローブ → コマンドラッチ)                 */
/*   3. STB=0 に戻す                                                        */
static void rtc_set_mode(u8 mode)
{
    /* STB=0, CLK=0, DI=0, C2C1C0=mode */
    outp(RTC_SET, mode & RTC_MODE_MASK);
    rtc_wait();

    /* STBパルス (0→1でモードセット) */
    outp(RTC_SET, (mode & RTC_MODE_MASK) | RTC_STB);
    rtc_wait();

    /* STB=0に戻す */
    outp(RTC_SET, mode & RTC_MODE_MASK);
    rtc_wait();
}

/* ======== データ読み出し (48ビット) ======== */
/*                                                                          */
/* レジスタシフトモード (C2C1C0=001) に設定済みの状態で呼ぶこと。            */
/* DATA OUTはシフトレジスタのLSBが出力される。                              */
/* CLKパルス毎にシフトレジスタが1ビット右シフトされ、次のビットが現れる。    */
/*                                                                          */
/* ポート0x33 bit0 (CDAT) = µPD4990A DATA OUT                               */
static void rtc_read_48bits(u8 *nibbles)
{
    int i;
    u8 bit;

    /* 最初のビット (bit0) は既に DATA OUT に出ている */
    bit = (u8)(inp(RTC_READ) & 0x01);
    nibbles[0] = bit;

    /* 残り47ビットを CLK パルスでシフトしながら読む */
    for (i = 1; i < RTC_READ_BITS; i++) {
        /* CLK=1 (レジスタシフトモードのC0=1をキープ) */
        outp(RTC_SET, RTC_MODE_SHIFT | RTC_CLK);
        rtc_wait();

        /* CLK=0 (立ち下がりで次のビットがDATA OUTに現れる) */
        outp(RTC_SET, RTC_MODE_SHIFT);
        rtc_wait();

        /* データ読み出し */
        bit = (u8)(inp(RTC_READ) & 0x01);

        /* ニブル配列に格納 */
        if ((i % RTC_NIBBLE_BITS) == 0) {
            nibbles[i / RTC_NIBBLE_BITS] = bit;
        } else {
            nibbles[i / RTC_NIBBLE_BITS] |= (bit << (i % RTC_NIBBLE_BITS));
        }
    }
}

/* ======================================================================== */
/*  公開API                                                                 */
/* ======================================================================== */

void rtc_init(void)
{
    /* レジスタホールドモードに設定 (初期状態) */
    rtc_set_mode(RTC_MODE_HOLD);
}

void rtc_read(RTC_Time *t)
{
    u8 nib[RTC_NIBBLE_COUNT];   /* 12ニブル (48ビット) */

    /* 1. タイムリード: カウンタの現在値をシフトレジスタにコピー */
    rtc_set_mode(RTC_MODE_TIMEREAD);
    rtc_wait();

    /* 2. レジスタシフトモード: DATA OUTをシフトレジスタLSBにする */
    rtc_set_mode(RTC_MODE_SHIFT);
    rtc_wait();

    /* 3. 48ビット読み出し */
    rtc_read_48bits(nib);

    /* 4. レジスタホールドに戻す (通常カウント再開) */
    rtc_set_mode(RTC_MODE_HOLD);

    /*
     * ニブル配置 (LSBファースト):
     *  nib[0]  = 1秒    nib[1]  = 10秒
     *  nib[2]  = 1分    nib[3]  = 10分
     *  nib[4]  = 1時    nib[5]  = 10時
     *  nib[6]  = 1日    nib[7]  = 10日
     *  nib[8]  = 曜日   nib[9]  = 月
     *  nib[10] = 1年    nib[11] = 10年
     */
    t->sec   = (nib[RTC_NIB_SEC_TENS] & RTC_SECMIN_TENS_MASK) * 10 + (nib[RTC_NIB_SEC_ONES] & RTC_DIGIT_MASK);
    t->min   = (nib[RTC_NIB_MIN_TENS] & RTC_SECMIN_TENS_MASK) * 10 + (nib[RTC_NIB_MIN_ONES] & RTC_DIGIT_MASK);
    t->hour  = (nib[RTC_NIB_HOUR_TENS] & RTC_HOURDAY_TENS_MASK) * 10 + (nib[RTC_NIB_HOUR_ONES] & RTC_DIGIT_MASK);
    t->day   = (nib[RTC_NIB_DAY_TENS] & RTC_HOURDAY_TENS_MASK) * 10 + (nib[RTC_NIB_DAY_ONES] & RTC_DIGIT_MASK);
    t->wday  = nib[RTC_NIB_WDAY] & RTC_WDAY_MASK;
    t->month = nib[RTC_NIB_MONTH] & RTC_DIGIT_MASK;
    t->year  = (nib[RTC_NIB_YEAR_TENS] & RTC_DIGIT_MASK) * 10 + (nib[RTC_NIB_YEAR_ONES] & RTC_DIGIT_MASK);

    /* 範囲チェック */
    if (t->sec > RTC_SECMIN_MAX) t->sec = 0;
    if (t->min > RTC_SECMIN_MAX) t->min = 0;
    if (t->hour > 23) t->hour = 0;
    if (t->day < 1 || t->day > 31) t->day = 1;
    if (t->month < 1 || t->month > 12) t->month = 1;
}

u32 rtc_uptime_sec(void)
{
    return tick_count / PIT_HZ;  /* PIT_HZ Hz → 秒変換 */
}
