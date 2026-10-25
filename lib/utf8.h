/* ======================================================================== */
/*  UTF8.H — UTF-8デコーダ / Unicode→JIS変換 / u32最適化プリミティブ         */
/*                                                                          */
/*  システム標準エンコーディング: UTF-8                                        */
/* ======================================================================== */

#ifndef UTF8_H
#define UTF8_H

/* 基本型 (u8/u16/u32) の出どころ。カーネルビルド時は include/types.h、
   ユーザー空間では SDK の契約ヘッダから取る。utf8.c はカーネルと
   外部プログラムの双方でビルドされるため、両対応にしておく。 */
#ifdef __KERNEL_BUILD__
#include "types.h"
#else
#include "os32_kapi_shared.h"
#endif

/* UTF-8 wire bytes. LEADn_MASK/TAG recognize prefixes; MIN/MAX are strict. */
#define UTF8_ASCII_LIMIT       0x80
#define UTF8_CONT_MASK         0xC0
#define UTF8_CONT_TAG          0x80
#define UTF8_CONT_PAYLOAD_MASK 0x3F
#define UTF8_CONT_PAYLOAD_BITS 6
#define UTF8_LEAD2_MASK        0xE0
#define UTF8_LEAD2_TAG         0xC0
#define UTF8_LEAD2_MIN         0xC2
#define UTF8_LEAD2_MAX         0xDF
#define UTF8_LEAD2_PAYLOAD_MASK 0x1F
#define UTF8_LEAD3_MASK        0xF0
#define UTF8_LEAD3_TAG         0xE0
#define UTF8_LEAD3_MIN         UTF8_LEAD3_TAG
#define UTF8_LEAD3_MAX         0xEF
#define UTF8_LEAD3_PAYLOAD_MASK 0x0F
#define UTF8_LEAD4_MASK        0xF8
#define UTF8_LEAD4_TAG         0xF0
#define UTF8_LEAD4_MIN         UTF8_LEAD4_TAG
#define UTF8_LEAD4_MAX         0xF4
#define UTF8_LEAD4_PAYLOAD_MASK 0x07
#define UTF8_LEN2              2
#define UTF8_LEN3              3
#define UTF8_LEN4              4
#define UTF8_MAX_BYTES         UTF8_LEN4
#define UTF8_PACKED2_MASK      0x0000FFFF
#define UTF8_PACKED3_MASK      0x00FFFFFF
/* Strict BMP validation: E0 needs a second byte >= A0; ED needs < A0. */
#define UTF8_LEAD3_SURROGATE   0xED
#define UTF8_SECOND3_MIN       0xA0
#define UTF8_IS_CONT(b) (((b) & UTF8_CONT_MASK) == UTF8_CONT_TAG)

/* Unicode codepoint ranges (distinct from encoded byte values). */
#define UCS_BOM                      0xFEFF
#define UCS_C0_END                   0x20
#define UCS_REPLACEMENT_CHAR         0xFFFD
#define UCS_UTF8_3_MIN               0x800
#define UCS_SURROGATE_FIRST          0xD800
#define UCS_SURROGATE_LAST           0xDFFF
#define UCS_ASCII_PRINT_FIRST        UCS_C0_END
#define UCS_ASCII_PRINT_LAST         0x7E
#define UCS_FULLWIDTH_ASCII_FIRST    0xFF01
#define UCS_FULLWIDTH_ASCII_LAST     0xFF5E
#define UCS_HALFWIDTH_KANA_FIRST     0xFF61
#define UCS_HALFWIDTH_KANA_LAST      0xFF9F
#define JIS_KANA_FIRST               0xA1
#define JIS_CODE_MIN 0x21
#define JIS_CODE_MAX 0x7E
#define UCS_HIRAGANA_FIRST 0x3041
#define UCS_HIRAGANA_LAST 0x3093
#define UCS_KATAKANA_FIRST 0x30A1
#define UCS_KATAKANA_LAST 0x30F6
#define JIS_HIRAGANA_BASE 0x2421
#define JIS_KATAKANA_BASE 0x2521

/* UTF-8デコード結果 */
typedef struct {
    u32 codepoint;    /* Unicodeコードポイント (0-0x10FFFF) */
    int bytes_used;   /* 消費バイト数 (1-4, エラー時1) */
} utf8_decode_t;

/* ======================================================================== */
/*  u32 最適化プリミティブ                                                   */
/*                                                                          */
/*  日本語UTF-8文字 (3B) の比較を u32 一括読み出し＋マスクで高速化する。      */
/*  i386のアンアラインドu32読み出しペナルティは小さいため有効。               */
/* ======================================================================== */

/* バイトスワップ (i386手動実装 — bswap命令はi486+のため使用不可) */
#define utf8_bswap32(v) \
    ((((u32)(v) & 0xFF000000u) >> 24) | \
     (((u32)(v) & 0x00FF0000u) >>  8) | \
     (((u32)(v) & 0x0000FF00u) <<  8) | \
     (((u32)(v) & 0x000000FFu) << 24))

/* パック済みu32文字の等価比較 */
#define utf8_eq32(a, b)  ((a) == (b))

/* UTF-8先頭1文字のバイト数を返す (1~4, 不正バイト時1) */
int utf8_char_bytes(const u8 *p);

/* UTF-8の1文字をu32にパック (リトルエンディアン、ゼロパディング) */
u32 utf8_pack32(const u8 *p);

/* パック済みu32文字の辞書順比較 (-1, 0, +1) */
int utf8_cmp32(u32 a, u32 b);

/* ======================================================================== */
/*  デコード・変換                                                           */
/* ======================================================================== */

/* UTF-8 → Unicode デコード (1文字分) */
utf8_decode_t utf8_decode(const u8 *src);

/* Unicode → JIS変換 (0=変換不可) */
u16 unicode_to_jis(u32 codepoint);

/* Unicode → ANKコード (0=非ANK) */
u8 unicode_to_ank(u32 codepoint);

/* ======================================================================== */
/*  UTF-8 文字列ヘルパー                                                     */
/* ======================================================================== */

/* UTF-8文字列の文字数を返す (バイト数ではなく文字数) */
int utf8_strlen(const u8 *s);

/* UTF-8文字列の末尾1文字を削除 (ヌル終端を書き込む) */
void utf8_delete_last(u8 *s);

/* UTF-8文字列のn番目(0始まり)の文字の先頭ポインタを返す (NULL=範囲外) */
const u8 *utf8_char_at(const u8 *s, int n);

#endif /* UTF8_H */
