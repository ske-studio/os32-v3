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
#include "console.h"

/* ======== 数値→文字列変換 (kprintf.h で公開) ======== */

/* 符号なし10進を文字列バッファに変換 (末尾NUL付き) */
/* 戻り値: 桁数 */
int kutoa_dec(u32 val, char *buf, int bufsz)
{
    int i = bufsz - 1;
    int len;
    buf[i] = '\0';
    i--;
    if (val == 0) {
        buf[i] = '0';
        i--;
    } else {
        while (val > 0 && i >= 0) {
            buf[i] = '0' + (char)(val % 10);
            val /= 10;
            i--;
        }
    }
    len = bufsz - 2 - i;
    /* バッファ先頭に移動 */
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

/* 符号なし16進を文字列バッファに変換 */
int kutoa_hex(u32 val, char *buf, int bufsz, int upper)
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
            buf[i] = digits[val & 0xF];
            val >>= 4;
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

/* ======== 内部: パディング付き出力 ======== */

/* 文字列をパディング付きで出力 */
static void emit_padded(const char *str, int len, int width, 
                        char pad_char, int left_align, u8 attr)
{
    int pad;
    char pad_buf[2];
    pad_buf[1] = '\0';
    pad_buf[0] = pad_char;

    pad = width - len;
    if (pad < 0) pad = 0;

    if (!left_align) {
        /* 右寄せ: 先にパディング */
        while (pad > 0) {
            shell_print(pad_buf, attr);
            pad--;
        }
    }

    /* 本体出力 */
    shell_print(str, attr);

    if (left_align) {
        /* 左寄せ: 後でスペースパディング (ゼロパディングは左寄せと併用しない) */
        pad_buf[0] = ' ';
        while (pad > 0) {
            shell_print(pad_buf, attr);
            pad--;
        }
    }
}

/* ======== 公開API ======== */

void __cdecl kprintf(u8 attr, const char *fmt, ...)
{
    u32 *args = (u32 *)(&fmt) + 1;
    int ai = 0;
    const char *p = fmt;

    /* 属性は **ここで 1 回だけ** PC-98 のテキスト属性へ直す。
     * 呼び出し側の大半は PC/AT (CGA) 流の 0x07 などを渡しており、
     * それを属性 VRAM へ直書きすると画面に何も出ない
     * (lib/kprintf_attr.c の冒頭に経緯)。下流 (shell_print /
     * shell_print_utf8 / console_write) は全部この attr を使うので、
     * 変換を入口に置けば取りこぼしが無い。 */
    attr = kprintf_attr_to_pc98(attr);

    while (*p) {
        if (*p == '%' && p[1]) {
            int width = 0;
            int left_align = 0;
            char pad_char = ' ';
            char numbuf[16];
            int len;

            p++;  /* '%' をスキップ */

            /* フラグ解析 */
            if (*p == '-') {
                left_align = 1;
                p++;
            }
            if (*p == '0') {
                pad_char = '0';
                p++;
            }

            /* 幅解析 */
            while (*p >= '0' && *p <= '9') {
                width = width * 10 + (*p - '0');
                p++;
            }

            /* 長さ修飾子 (l を無視、32bit環境で同義) */
            if (*p == 'l') {
                p++;
            }

            /* 変換指定子 */
            switch (*p) {
                case 'd':
                case 'i': {
                    int val = (int)args[ai++];
                    int neg = 0;
                    u32 uval;
                    if (val < 0) {
                        neg = 1;
                        uval = (u32)(-val);
                    } else {
                        uval = (u32)val;
                    }
                    len = kutoa_dec(uval, numbuf + 1, 14);
                    if (neg) {
                        /* 符号の処理 */
                        if (pad_char == '0' && !left_align) {
                            /* ゼロパディング: 符号を先に出力 */
                            shell_print("-", attr);
                            emit_padded(numbuf + 1, len, width - 1,
                                        pad_char, left_align, attr);
                        } else {
                            numbuf[0] = '-';
                            emit_padded(numbuf, len + 1, width,
                                        pad_char, left_align, attr);
                        }
                    } else {
                        emit_padded(numbuf + 1, len, width,
                                    pad_char, left_align, attr);
                    }
                    break;
                }
                case 'u': {
                    u32 val = args[ai++];
                    len = kutoa_dec(val, numbuf, 15);
                    emit_padded(numbuf, len, width,
                                pad_char, left_align, attr);
                    break;
                }
                case 'x':
                case 'X': {
                    u32 val = args[ai++];
                    int upper = (*p == 'X');
                    len = kutoa_hex(val, numbuf, 15, upper);
                    emit_padded(numbuf, len, width,
                                pad_char, left_align, attr);
                    break;
                }
                case 's': {
                    const char *str = (const char *)args[ai++];
                    int slen;
                    const char *s;
                    if (!str) str = "(null)";
                    /* 文字列長を計算 */
                    slen = 0;
                    s = str;
                    while (*s) { slen++; s++; }
                    if (width > 0) {
                        /* パディング付き出力 (UTF-8はshell_print内で処理) */
                        int pad = width - slen;
                        if (pad < 0) pad = 0;
                        if (!left_align) {
                            char sp[2] = {' ', '\0'};
                            while (pad > 0) { shell_print(sp, attr); pad--; }
                        }
                        shell_print_utf8(str, attr);
                        if (left_align) {
                            char sp[2] = {' ', '\0'};
                            while (pad > 0) { shell_print(sp, attr); pad--; }
                        }
                    } else {
                        shell_print_utf8(str, attr);
                    }
                    break;
                }
                case 'c': {
                    char ch_buf[2];
                    ch_buf[0] = (char)args[ai++];
                    ch_buf[1] = '\0';
                    shell_print(ch_buf, attr);
                    break;
                }
                case '%':
                    shell_print("%", attr);
                    break;
                default: {
                    /* 不明な指定子: そのまま出力。
                     * 呼び出し側は指定子ぶんの実引数を積んでいる前提なので、
                     * 消費しないと以降の %d/%s が 1 個ずつずれて別の引数を
                     * (最悪ポインタとして) 読んでしまう。 */
                    char tmp[2];
                    tmp[0] = *p;
                    tmp[1] = '\0';
                    ai++;
                    shell_print(tmp, attr);
                    break;
                }
            }
        } else {
            const char *start = p;
            while (*p && *p != '%') p++;
            if (p == start) {
                /* 末尾の '%' (p[1]=='\0' なので上の分岐に入らない)。
                 * ここで進めないと *p が '%' のまま無限ループする。 */
                console_write(p, 1, attr);
                p++;
                continue;
            }
            console_write(start, (u32)(p - start), attr);
            continue;
        }
        p++;
    }
}
