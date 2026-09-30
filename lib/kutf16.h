/* ======================================================================== */
/*  KUTF16.H — UTF-8 ↔ UTF-16LE 変換ユーティリティ                        */
/*                                                                          */
/*  hostdrvfs用。BMP範囲 (U+0000〜U+FFFF) のみサポート。                    */
/* ======================================================================== */

#ifndef KUTF16_H
#define KUTF16_H

#include "types.h"

/* UTF-8 → UTF-16LE 変換
 * utf8:      入力UTF-8文字列 (NULL終端)
 * utf16:     出力UTF-16LEバッファ
 * max_words: utf16バッファの最大WORD数 (NULL終端分含む)
 * 戻り値:    書き込んだWORD数 (NULL終端含む, 1 以上)。
 *            不正な UTF-8 (途中で切れた列・単独の継続バイト・冗長な符号化・
 *            サロゲート・4 バイト列 = BMP 外)、収まらない入力、引数の誤りは
 *            **-1** (置換も切り詰めもしない。utf16 の中身は不定)
 */
int kutf8_to_utf16le(const char *utf8, u16 *utf16, int max_words);

/* UTF-16LE → UTF-8 変換
 * utf16:      入力UTF-16LE配列
 * utf16_len:  入力のバイト数 (NULL終端含まず)
 * utf8:       出力UTF-8バッファ
 * max_bytes:  utf8バッファの最大バイト数 (NULL終端分含む)
 * 戻り値:     書き込んだバイト数 (NULL終端含む), エラー時0
 */
int kutf16le_to_utf8(const u16 *utf16, int utf16_len,
                     char *utf8, int max_bytes);

#endif /* KUTF16_H */
