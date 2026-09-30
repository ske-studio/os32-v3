/* ======================================================================== */
/*  KUTF16.C — UTF-8 ↔ UTF-16LE 変換ユーティリティ                        */
/*                                                                          */
/*  BMP範囲 (U+0000〜U+FFFF) のみサポート。サロゲートペア非対応。          */
/*  UTF-8 → UTF-16 は不正な列・BMP 外を -1 で断る (置換しない)。           */
/* ======================================================================== */

#include "kutf16.h"

/* ------------------------------------------------------------------------ */
/*  kutf8_to_utf16le — UTF-8 → UTF-16LE 変換                               */
/*                                                                          */
/*  **厳密**: 最短形の正しい UTF-8 で、BMP (サロゲート U+D800〜U+DFFF を     */
/*  除く) の文字だけを変換する。途中で切れた列・単独の継続バイト・冗長な    */
/*  符号化 (0xC0 0xAE 等)・4 バイト列 (BMP 外)・出力に収まらない入力は、     */
/*  **何も成功させずに -1** を返す。                                        */
/*                                                                          */
/*  以前は途中で切れた列で打ち切り、冗長な符号化を受理し、BMP 外を U+FFFD  */
/*  に置き換えていた。HostDrv ではその結果が「検査を通った名前とは別の      */
/*  名前」としてホストに届き、VFS の BUSY / pinned (名前の比較) をすり抜けて */
/*  使用中の実体を消せた (TASK_VFS_FD_PATH 実装レビュー ラリー 3、Codex B2 — */
/*  "disk.img\xC2" が "disk.img" に、"\xC1\xA4isk.img" が "disk.img" に、  */
/*  絵文字 2 種が同じ U+FFFD に)。名前の入口検査 (fs/vfs_name_rules.inc の   */
/*  VFS_NAME_RULE_WIN32) と同じ規則なので、検査を通った名前はここで必ず     */
/*  1 対 1 に変換される。                                                   */
/* ------------------------------------------------------------------------ */
int kutf8_to_utf16le(const char *utf8, u16 *utf16, int max_words)
{
    const u8 *s = (const u8 *)utf8;
    int out = 0;
    u32 cp;
    u8 c;

    if (!utf8 || !utf16 || max_words < 1) return -1;

    while (*s) {
        c = *s;
        if (c < 0x80) {
            cp = c;
            s += 1;
        } else if (c >= 0xC2 && c <= 0xDF) {
            /* 2 バイト: U+0080〜U+07FF (0xC0 / 0xC1 は冗長な符号化) */
            if ((s[1] & 0xC0) != 0x80) return -1;
            cp = ((u32)(c & 0x1F) << 6) | (u32)(s[1] & 0x3F);
            s += 2;
        } else if (c >= 0xE0 && c <= 0xEF) {
            /* 3 バイト: U+0800〜U+FFFF (サロゲートを除く) */
            if ((s[1] & 0xC0) != 0x80) return -1;
            if ((s[2] & 0xC0) != 0x80) return -1;
            cp = ((u32)(c & 0x0F) << 12) | ((u32)(s[1] & 0x3F) << 6) |
                 (u32)(s[2] & 0x3F);
            if (cp < 0x800) return -1;                      /* 冗長 */
            if (cp >= 0xD800 && cp <= 0xDFFF) return -1;    /* サロゲート */
            s += 3;
        } else {
            /* 単独の継続バイト (0x80〜0xBF)、0xC0 / 0xC1、4 バイト以上
             * (BMP 外: UTF-16 ではサロゲートペアが要るが未対応) */
            return -1;
        }
        if (out >= max_words - 1) return -1;   /* 収まらない: 切り詰めない */
        utf16[out++] = (u16)cp;
    }
    utf16[out++] = 0;   /* NULL終端 */
    return out;
}

/* ------------------------------------------------------------------------ */
/*  kutf16le_to_utf8 — UTF-16LE → UTF-8 変換                               */
/* ------------------------------------------------------------------------ */
int kutf16le_to_utf8(const u16 *utf16, int utf16_len,
                     char *utf8, int max_bytes)
{
    int i;
    int words = utf16_len / 2;  /* バイト数→WORD数 */
    int out = 0;
    u16 cp;

    if (!utf16 || !utf8 || max_bytes < 1) return 0;

    for (i = 0; i < words; i++) {
        cp = utf16[i];
        if (cp == 0) break;

        if (cp < 0x80) {
            /* ASCII */
            if (out + 1 >= max_bytes) break;
            utf8[out++] = (char)cp;
        } else if (cp < 0x800) {
            /* 2バイトUTF-8 */
            if (out + 2 >= max_bytes) break;
            utf8[out++] = (char)(0xC0 | (cp >> 6));
            utf8[out++] = (char)(0x80 | (cp & 0x3F));
        } else {
            /* 3バイトUTF-8 */
            if (out + 3 >= max_bytes) break;
            utf8[out++] = (char)(0xE0 | (cp >> 12));
            utf8[out++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            utf8[out++] = (char)(0x80 | (cp & 0x3F));
        }
    }
    utf8[out++] = '\0';
    return out;
}
