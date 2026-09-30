/* ======================================================================== */
/*  DBGSERIAL.C — デバッグ用シリアル出力ライブラリ (実装)                   */
/*                                                                          */
/*  OS32_DBG_SERIAL が定義されている場合のみコンパイルされる。               */
/*  KAPIの serial_* 関数群を薄くラップし、printf風のフォーマット出力と       */
/*  hex dump 機能を提供する。                                                */
/*                                                                          */
/*  リンク時にこの .o を含めるだけで使用可能。                               */
/*  リリース時は dbgserial.h が全マクロを空にするため、                      */
/*  このファイル自体をビルドから外してもよいし、残しても問題ない              */
/*  (未参照の関数は --gc-sections で除去される)。                            */
/* ======================================================================== */

/* 常に有効としてコンパイル (ヘッダのマクロガードを通すため) */
#define OS32_DBG_SERIAL
#include "dbgserial.h"

#include <stdarg.h>

/* ======== 内部状態 ======== */
static KernelAPI *dbg_api = (void *)0;

/* ======================================================================== */
/*  dbg_serial_init — デフォルトボーレート (38400) で初期化                  */
/* ======================================================================== */
void dbg_serial_init(KernelAPI *api)
{
    dbg_serial_init_baud(api, 38400);
}

/* ======================================================================== */
/*  dbg_serial_init_baud — 指定ボーレートで初期化                           */
/* ======================================================================== */
void dbg_serial_init_baud(KernelAPI *api, unsigned long baud)
{
    dbg_api = api;
    if (!dbg_api->serial_is_initialized()) {
        dbg_api->serial_init(baud);
    }
}

/* ======================================================================== */
/*  dbg_serial_putchar — 1文字出力                                          */
/* ======================================================================== */
void dbg_serial_putchar(char ch)
{
    if (!dbg_api) return;
    dbg_api->serial_putchar((u8)ch);
}

/* ======================================================================== */
/*  dbg_serial_print — 文字列出力 (改行なし)                                */
/* ======================================================================== */
void dbg_serial_print(const char *s)
{
    if (!dbg_api) return;
    dbg_api->serial_puts(s);
}

/* ======================================================================== */
/*  dbg_serial_puts — 文字列出力 + \r\n                                     */
/* ======================================================================== */
void dbg_serial_puts(const char *s)
{
    if (!dbg_api) return;
    dbg_api->serial_puts(s);
    dbg_api->serial_putchar('\r');
    dbg_api->serial_putchar('\n');
}

/* ======================================================================== */
/*  内部: 符号なし整数を文字列に変換                                        */
/* ======================================================================== */
static void _dbg_put_uint(unsigned long val, int base, int upper)
{
    char buf[12];   /* 32bit最大: 4294967295 (10桁) + NUL */
    int i = 0;
    const char *hex_lo = "0123456789abcdef";
    const char *hex_up = "0123456789ABCDEF";
    const char *digits;

    digits = upper ? hex_up : hex_lo;

    if (val == 0) {
        dbg_api->serial_putchar('0');
        return;
    }

    while (val > 0 && i < 11) {
        buf[i++] = digits[val % base];
        val /= base;
    }

    /* 逆順に出力 */
    while (i > 0) {
        dbg_api->serial_putchar((u8)buf[--i]);
    }
}

/* ======================================================================== */
/*  dbg_serial_printf — printf風フォーマット出力                             */
/*                                                                          */
/*  対応フォーマット: %d %u %x %X %s %c %% %p %ld %lu %lx                  */
/*  末尾に \r\n を自動付加する。                                             */
/* ======================================================================== */
void dbg_serial_printf(const char *fmt, ...)
{
    va_list ap;
    const char *p;
    int is_long;

    if (!dbg_api) return;

    va_start(ap, fmt);

    for (p = fmt; *p; p++) {
        if (*p != '%') {
            dbg_api->serial_putchar((u8)*p);
            continue;
        }

        p++;
        is_long = 0;

        /* 'l' プレフィックス */
        if (*p == 'l') {
            is_long = 1;
            p++;
        }

        switch (*p) {
        case 'd': {
            long val;
            val = is_long ? va_arg(ap, long) : (long)va_arg(ap, int);
            if (val < 0) {
                dbg_api->serial_putchar('-');
                val = -val;
            }
            _dbg_put_uint((unsigned long)val, 10, 0);
            break;
        }
        case 'u': {
            unsigned long val;
            val = is_long ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned int);
            _dbg_put_uint(val, 10, 0);
            break;
        }
        case 'x': {
            unsigned long val;
            val = is_long ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned int);
            _dbg_put_uint(val, 16, 0);
            break;
        }
        case 'X': {
            unsigned long val;
            val = is_long ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned int);
            _dbg_put_uint(val, 16, 1);
            break;
        }
        case 'p': {
            unsigned long val;
            val = (unsigned long)va_arg(ap, void *);
            dbg_api->serial_putchar('0');
            dbg_api->serial_putchar('x');
            _dbg_put_uint(val, 16, 0);
            break;
        }
        case 's': {
            const char *s;
            s = va_arg(ap, const char *);
            if (s) {
                dbg_api->serial_puts(s);
            } else {
                dbg_api->serial_puts("(null)");
            }
            break;
        }
        case 'c': {
            int ch;
            ch = va_arg(ap, int);
            dbg_api->serial_putchar((u8)ch);
            break;
        }
        case '%':
            dbg_api->serial_putchar('%');
            break;
        case '\0':
            /* フォーマット文字列が % で終わっている場合 */
            goto done;
        default:
            /* 不明なフォーマット → そのまま出力 */
            dbg_api->serial_putchar('%');
            dbg_api->serial_putchar((u8)*p);
            break;
        }
    }

done:
    /* 末尾に改行を付加 */
    dbg_api->serial_putchar('\r');
    dbg_api->serial_putchar('\n');

    va_end(ap);
}

/* ======================================================================== */
/*  dbg_serial_hexdump — メモリの16進ダンプ                                 */
/*                                                                          */
/*  出力フォーマット (1行16バイト):                                         */
/*    XXXXXXXX: XX XX XX XX XX XX XX XX  XX XX XX XX XX XX XX XX  |AAAAAAA| */
/* ======================================================================== */
void dbg_serial_hexdump(const void *addr, int len)
{
    const u8 *data;
    int i, j;
    u8 b;

    if (!dbg_api || !addr || len <= 0) return;

    data = (const u8 *)addr;

    for (i = 0; i < len; i += 16) {
        /* アドレス */
        _dbg_put_uint((unsigned long)(u32)(data + i), 16, 1);
        dbg_api->serial_putchar(':');
        dbg_api->serial_putchar(' ');

        /* ヘックスバイト */
        for (j = 0; j < 16; j++) {
            if (i + j < len) {
                b = data[i + j];
                dbg_api->serial_putchar("0123456789ABCDEF"[b >> 4]);
                dbg_api->serial_putchar("0123456789ABCDEF"[b & 0xF]);
            } else {
                dbg_api->serial_putchar(' ');
                dbg_api->serial_putchar(' ');
            }
            dbg_api->serial_putchar(' ');
            if (j == 7) {
                dbg_api->serial_putchar(' ');
            }
        }

        /* ASCII表示 */
        dbg_api->serial_putchar(' ');
        dbg_api->serial_putchar('|');
        for (j = 0; j < 16; j++) {
            if (i + j < len) {
                b = data[i + j];
                dbg_api->serial_putchar((b >= 0x20 && b <= 0x7E) ? (char)b : '.');
            } else {
                dbg_api->serial_putchar(' ');
            }
        }
        dbg_api->serial_putchar('|');
        dbg_api->serial_putchar('\r');
        dbg_api->serial_putchar('\n');
    }
}

/* ======================================================================== */
/*  安全なメモリダンプ — ページテーブル属性チェック付き                      */
/*                                                                          */
/*  NOT PRESENTページへのアクセスによるPage Faultを事前回避する。            */
/*  チェック手順:                                                           */
/*    1. 静的ブラックリスト (既知のガードページ/予約域)                      */
/*    2. KAPI paging_is_present() でページテーブル属性を照会                 */
/* ======================================================================== */

/* デフォルトダンプサイズ */
#define DBG_MEMDUMP_DEFAULT  256
/* 最大ダンプサイズ */
#define DBG_MEMDUMP_MAX      1024

/* 静的ブラックリスト: 既知のNOT PRESENTページ */
static int _dbg_is_blacklisted(u32 addr)
{
    /* カーネルスタックガード (0x1FB000。低位から移設済み) */
    if (addr >= 0x1FB000UL && addr <= 0x1FBFFFUL) return 1;
    /* カーネル予約域 */
    if (addr >= 0x200000UL && addr <= 0x2FFFFFUL) return 1;
    /* シェルスタックガード */
    if (addr >= 0x370000UL && addr <= 0x370FFFUL) return 1;
    /* シェル帯域後の予約 */
    if (addr >= 0x380000UL && addr <= 0x3FFFFFUL) return 1;
    /* GUARD A (sbrk上限ガード) */
    if (addr >= 0x500000UL && addr <= 0x500FFFUL) return 1;
    return 0;
}

/* ページ安全チェック: 指定範囲の全ページがアクセス可能か */
static int _dbg_check_pages(u32 addr, int len)
{
    u32 page_start;
    u32 page_end;
    u32 page;

    page_start = addr & ~0xFFFUL;
    page_end = (addr + (u32)len - 1) & ~0xFFFUL;

    for (page = page_start; page <= page_end; page += 0x1000UL) {
        /* ブラックリストチェック */
        if (_dbg_is_blacklisted(page)) {
            dbg_serial_printf("[MEMDUMP] BLOCKED: 0x%lX is in guard/reserved zone",
                              (unsigned long)page);
            return 0;
        }

        /* ページテーブル属性チェック (KAPI経由) */
        if (!dbg_api->paging_is_present(page)) {
            dbg_serial_printf("[MEMDUMP] BLOCKED: 0x%lX is NOT PRESENT",
                              (unsigned long)page);
            return 0;
        }
    }
    return 1;
}

/* ======================================================================== */
/*  dbg_memdumpn — 安全なメモリダンプ (長さ指定, 最大1024バイト)             */
/* ======================================================================== */
void dbg_memdumpn(u32 addr, int len)
{
    if (!dbg_api) return;

    /* サイズ制限 */
    if (len <= 0) len = DBG_MEMDUMP_DEFAULT;
    if (len > DBG_MEMDUMP_MAX) len = DBG_MEMDUMP_MAX;
    len = (len + 15) & ~15; /* 16バイトに切り上げ */

    dbg_serial_printf("[MEMDUMP] 0x%lX (%d bytes)", (unsigned long)addr, len);

    /* 安全チェック */
    if (!_dbg_check_pages(addr, len)) return;

    /* ダンプ実行 */
    dbg_serial_hexdump((const void *)addr, len);
    dbg_serial_puts("[MEMDUMP] done.");
}

/* ======================================================================== */
/*  dbg_memdump — 安全なメモリダンプ (256バイト固定)                         */
/* ======================================================================== */
void dbg_memdump(u32 addr)
{
    dbg_memdumpn(addr, DBG_MEMDUMP_DEFAULT);
}
