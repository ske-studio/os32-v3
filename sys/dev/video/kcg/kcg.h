/* ======================================================================== */
/*  KCG.H — 漢字キャラクタジェネレータ (KCG) ドライバ                       */
/*                                                                          */
/*  PC-98のフォントROMからANK(8x16)・漢字(16x16)パターンを読み出し          */
/*  グラフィックVRAM上に日本語テキストを描画する                              */
/*                                                                          */
/*  出典: PC9800Bible §2-6-5 表2-29                                         */
/* ======================================================================== */

#ifndef __KCG_H
#define __KCG_H

#include "types.h"
#include "pc98.h"

/* ======== I/Oポート ======== */
#define KCG_CODE_LO   0xA1    /* 文字コード第2バイト (JIS下位) */
#define KCG_CODE_HI   0xA3    /* 文字コード第1バイト (JIS上位-0x20) */
#define KCG_LINE_SEL  0xA5    /* 読出しライン指定 */
#define KCG_DATA      0xA9    /* パターンデータ読出し/書込み */

/* ======== CGウィンドウ ======== */
#define CG_WINDOW     TVRAM_CG_WINDOW   /* CGウィンドウ先頭アドレス */

#include "kcg_contract.h"

#define KCG_LINE_LEFT 0x20

#endif /* __KCG_H */
