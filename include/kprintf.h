/* ======================================================================== */
/*  KPRINTF.H — 書式付き出力ライブラリヘッダ                                */
/* ======================================================================== */

#ifndef __KPRINTF_H
#define __KPRINTF_H

#include "types.h"

void __cdecl kprintf(u8 attr, const char *fmt, ...);

/* kprintf の属性 1 バイトを PC-98 のテキスト属性へ直す (純粋関数)。
 * 実体は lib/kprintf_attr.c、試験は tools/tests/test_kprintf_attr.py。
 *
 * 呼び出し側の 70 か所以上が PC/AT (CGA) 流の 0x07 などを渡していて、
 * PC-98 の属性としてそのまま書かれると **画面に 1 文字も出ない**
 * (bit0 = 表示 / bit5,6,7 = 色。07h は「色無し + リバース + ブリンク」)。
 * kprintf の入口で 1 回だけ通す。既に PC-98 流の値 (0xC1 等) はそのまま。 */
u8 kprintf_attr_to_pc98(u8 attr);

/* 数値→文字列変換 (kprintf 内部でも使用)。
 * buf に NUL 終端で書き、桁数を返す。bufsz はバッファ全体サイズ。
 * 桁揃えはしない — 固定幅表示が要る場合は呼び出し側で行う。 */
int kutoa_dec(u32 val, char *buf, int bufsz);
int kutoa_hex(u32 val, char *buf, int bufsz, int upper);

#endif /* __KPRINTF_H */
