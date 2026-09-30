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

/* ======== I/Oポート ======== */
#define KCG_CODE_LO   0xA1    /* 文字コード第2バイト (JIS下位) */
#define KCG_CODE_HI   0xA3    /* 文字コード第1バイト (JIS上位-0x20) */
#define KCG_LINE_SEL  0xA5    /* 読出しライン指定 */
#define KCG_DATA      0xA9    /* パターンデータ読出し/書込み */

/* ======== CGウィンドウ ======== */
#define CG_WINDOW     0xA4000UL   /* CGウィンドウ先頭アドレス */

/* ======== フォントサイズ ======== */
#define KCG_ANK_W     8       /* ANK文字幅 */
#define KCG_ANK_H     16      /* ANK文字高 */
#define KCG_KANJI_W   16      /* 漢字幅 */
#define KCG_KANJI_H   16      /* 漢字高 */

/* ======== API ======== */

/* KCG初期化 (コードアクセスモード設定) */
void kcg_init(void);

/* 描画スケール設定 (1=等倍, 2=2倍, 3=3倍, 4=4倍) */
void kcg_set_scale(int scale);

/* 現在のスケール値 */
extern int kcg_scale;

/* ANK文字パターン読み出し (8x16, 16バイト) */
void kcg_read_ank(u8 ch, u8 *buf);

/* 漢字パターン読み出し (16x16, 32バイト) */
/* jis_code = JISコード (例: 0x3021 = '亜') */
void kcg_read_kanji(u16 jis_code, u8 *buf);

/* ANK文字をバックバッファに描画 (8x16) */
void kcg_draw_ank(int x, int y, u8 ch, u8 fg, u8 bg);

/* 漢字をバックバッファに描画 (16x16) */
void kcg_draw_kanji(int x, int y, u16 jis_code, u8 fg, u8 bg);

/* Shift-JIS文字列をバックバッファに描画 */
/* ANK(半角)と漢字(全角)を自動判別 */
/* 戻り値: 描画した文字列のピクセル幅 */
int kcg_draw_sjis(int x, int y, const char *sjis_str, u8 fg, u8 bg);

/* UTF-8文字列をバックバッファに描画 (スケール対応) */
/* UTF-8 → Unicode → JIS/ANK変換して描画。lib/utf8.c の変換関数を使用 */
/* 戻り値: 描画した文字列のピクセル幅 */
int kcg_draw_utf8(int x, int y, const char *utf8_str, u8 fg, u8 bg);

/* 外部フォントファイル(.kcgfont)をロードしてキャッシュを上書き */
/* path: ゲスト側のフォントファイルパス (例: "/sys/font/default.kcgfont") */
/* 戻り値: 0=成功, 負数=エラー */
int kcg_load_font(const char *path);

#endif /* __KCG_H */
