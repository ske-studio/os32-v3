#ifndef KCG_CONTRACT_H
#define KCG_CONTRACT_H

#include "types.h"

/* ======== フォントサイズ ======== */
#define KCG_ANK_W     8       /* ANK文字幅 */
#define KCG_ANK_H     16      /* ANK文字高 */
#define KCG_KANJI_W   16      /* 漢字幅 */
#define KCG_KANJI_H   16      /* 漢字高 */

#define KCG_KANJI_BPL (KCG_KANJI_W / 8)
/* ======== API ======== */

/* スケール初期化。ROM/cache は既存 KCG 部品が持つ。 */
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

/* 旧 KAPI slot。起動中を含め常に NOSYS、副作用なし。 */
int kcg_load_font(const char *path);

#endif /* KCG_CONTRACT_H */
