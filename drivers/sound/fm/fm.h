/* ======================================================================== */
/*  FM.H — PC-9801-26K (YM2203/OPN) FM音源ドライバ定義                     */
/*  snddrv (DOS版) からOS32用に移植                                         */
/*  FM 3ch + SSG 3ch = 6ch同時発音                                          */
/* ======================================================================== */

#ifndef __FM_H
#define __FM_H

#include "fm_contract.h"

/* ======== OPN I/O ポート (PC9800Bible §2-13) ======== */
#define OPN_ADDR      0x0188   /* レジスタアドレス (Write) */
#define OPN_DATA      0x018A   /* レジスタデータ (Write) */
#define OPN_STATUS    0x0188   /* ステータス (Read) */

/* ======== OPN内部レジスタ ======== */
#define OPN_REG_TIMER_A_HI  0x24
#define OPN_REG_TIMER_A_LO  0x25
#define OPN_REG_TIMER_B     0x26
#define OPN_REG_TIMER_CTRL  0x27
#define OPN_REG_KEY_ONOFF   0x28
#define OPN_REG_PRESCALER   0x2D    /* プリスケーラ設定 */

/* FMチャンネルレジスタオフセット (ch + slot*4 で計算) */
#define OPN_REG_DT_ML       0x30    /* DT/MUL */
#define OPN_REG_TL          0x40    /* トータルレベル */
#define OPN_REG_KS_AR       0x50    /* キースケール/アタックレート */
#define OPN_REG_DR          0x60    /* ディケイレート */
#define OPN_REG_SR          0x70    /* サスティンレート */
#define OPN_REG_SL_RR       0x80    /* サスティンレベル/リリースレート */
#define OPN_REG_FNUM_LO     0xA0    /* F-Number 下位 */
#define OPN_REG_FNUM_HI     0xA4    /* Block/F-Number 上位 */
#define OPN_REG_FB_CON      0xB0    /* フィードバック/コネクション */

/* OPN ステータスビット */
#define OPN_BUSY            0x80    /* ビジーフラグ */

/* Key-ON スロットマスク */
#define OPN_KEY_ALL_SLOTS   0xF0    /* 全4スロットON */

/* TL値 */
#define OPN_TL_MAX          0x7F    /* 最大減衰 (無音) */

/* タイマー制御値 */
#define OPN_TIMER_STOP      0x30    /* タイマーA/B 停止 */

/* ======== SSGレジスタ (00H-0DH) ======== */
#define SSG_REG_TONE_A_LO   0x00
#define SSG_REG_TONE_A_HI   0x01
#define SSG_REG_TONE_B_LO   0x02
#define SSG_REG_TONE_B_HI   0x03
#define SSG_REG_TONE_C_LO   0x04
#define SSG_REG_TONE_C_HI   0x05
#define SSG_REG_NOISE        0x06
#define SSG_REG_MIXER        0x07
/* SSGミキサー定数 */
#define SSG_MIXER_ALL_OFF    0x3F    /* 全チャンネルOFF */
#define SSG_MIXER_IO_FLAG    0x80    /* I/Oポートフラグ */
#define SSG_REG_VOL_A        0x08
#define SSG_REG_VOL_B        0x09
#define SSG_REG_VOL_C        0x0A
#define SSG_REG_ENV_LO       0x0B
#define SSG_REG_ENV_HI       0x0C
#define SSG_REG_ENV_SHAPE    0x0D

/* ======== SSG周波数マクロ ======== */
/* Period = 3993600 / (4 × 16 × freq) = 62400 / freq */
#define SSG_PERIOD_CLOCK_HZ 62400UL
#define SSG_PERIOD(freq)  ((u16)(SSG_PERIOD_CLOCK_HZ / (freq)))

#endif /* __FM_H */
