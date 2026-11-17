/* ======================================================================== */
/*  KPRINTF.C — 書式付き出力ライブラリ (libc準拠フォーマット対応)            */
/*                                                                          */
/*  対応フォーマット指定子:                                                  */
/*    %d, %i  — 符号付き10進                                                */
/*    %u      — 符号なし10進                                                */
/*    %x      — 16進 (小文字)                                               */
/*    %X      — 16進 (大文字)                                               */
/*    %s      — 文字列 (UTF-8対応)                                          */
/*    %c      — 文字                                                        */
/*    %%      — リテラル '%'                                                 */
/*                                                                          */
/*  対応フラグ/幅:                                                          */
/*    %0Nd    — ゼロパディング (例: %02u → "05")                            */
/*    %Nd     — スペースパディング・右寄せ (例: %4d → "  42")               */
/*    %-Ns    — 左寄せ (例: %-10s → "hello     ")                           */
/*    %ld     — long (32bit環境では通常と同じ、互換性のため受理)             */
/*                                                                          */
/*  非対応: %lld / %llu (64bit)。'l' は 1 個しか読まないため %lld は        */
/*  "%ld + 'd' リテラル" に化け、引数もずれる。64bit 値を表示したい場合は   */
/*  上位/下位 32bit に分けて %x を 2 回使うこと。                           */
/* ======================================================================== */

#include "kprintf.h"
#include <stdarg.h>

#include "console.h"
#include "platform_console.h"   /* PLATFORM_KPRINTF_IS_CONSOLE (機種ごとの固定名) */

/* ======== 数値→文字列変換 (kprintf.h で公開) ======== */

/* 末尾から桁を生成し、バッファ先頭に詰める。 */
static int kutoa_base(u32 val, char *buf, int bufsz, int hex, int upper)
{
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int i = bufsz - 1;
    int len;
    buf[i] = '\0';
    i--;
    if (val == 0) {
        buf[i] = '0';
        i--;
    } else {
        while (val > 0 && i >= 0) {
            if (hex) {
                buf[i] = digits[val & 0xF];
                val >>= 4;
            } else {
                buf[i] = '0' + (char)(val % 10);
                val /= 10;
            }
            i--;
        }
    }
    len = bufsz - 2 - i;
    {
        int j;
        int start = i + 1;
        for (j = 0; j < len; j++) {
            buf[j] = buf[start + j];
        }
    }
    buf[len] = '\0';
    return len;
}

/* 符号なし10進を文字列バッファに変換 (末尾NUL付き)。戻り値: 桁数 */
int kutoa_dec(u32 val, char *buf, int bufsz)
{
    return kutoa_base(val, buf, bufsz, 0, 0);
}

/* 符号なし16進を文字列バッファに変換 */
int kutoa_hex(u32 val, char *buf, int bufsz, int upper)
{
    return kutoa_base(val, buf, bufsz, 1, upper);
}

/* ======== 公開API ======== */

/* Shared parser: leave *cursor at the conversion (or NUL). Unknown
 * conversions consume one slot, %% consumes none, exactly as the renderer. */
char kprintf_spec(const char **cursor, int *width, int *left_align,
                  char *pad_char)
{
    const char *p = *cursor;
    *width = 0;
    *left_align = 0;
    *pad_char = ' ';
    if (*p == '-') { *left_align = 1; p++; }
    if (*p == '0') { *pad_char = '0'; p++; }
    while (*p >= '0' && *p <= '9') {
        /* Keep malformed widths from overflowing signed arithmetic. */
        if (*width <= (0x7fffffff - 9) / 10)
            *width = *width * 10 + (*p - '0');
        p++;
    }
    if (*p == 'l') p++;
    *cursor = p;
    return *p;
}

/* ======== コンソール向けの実体 ======== */

#define KPRINTF_WRITE console_write
#define KPRINTF_PRINT shell_print
#define KPRINTF_PRINT_UTF8 shell_print_utf8
#define KPRINTF_VNAME console_vprintf
#define KPRINTF_NAME console_printf
#include "kprintf_format.inc"

#if PLATFORM_KPRINTF_IS_CONSOLE
/* カーネルの診断 (kprintf) もコンソールへ出す機種: 同じ関数の別名にする。 */
void kvprintf(u8 attr, const char *fmt, const u32 *args) __attribute__((alias("console_vprintf")));
void __cdecl kprintf(u8 attr, const char *fmt, ...) __attribute__((alias("console_printf")));
#endif
