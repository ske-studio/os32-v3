/* ======================================================================== */
/*  DBGSERIAL.H — デバッグ用シリアル出力ライブラリ                          */
/*                                                                          */
/*  RS-232C (KAPI serial_*) を利用したデバッグトレース出力。                 */
/*  リリース時に OS32_DBG_SERIAL の定義を除去するだけで、                    */
/*  全マクロが空に展開されゼロコスト化される。                               */
/*                                                                          */
/*  使い方:                                                                 */
/*    1. プログラム先頭で OS32_DBG_SERIAL を定義 (dbgserial.h より前)         */
/*    2. dbg_init(kapi) を main() 冒頭で呼ぶ                                */
/*    3. DBG(), DBGF() マクロで出力                                          */
/*    4. リリース時は #define OS32_DBG_SERIAL を削除 (またはコメントアウト)   */
/*                                                                          */
/*  例:                                                                     */
/*    #define OS32_DBG_SERIAL                                                */
/*    #include "rt/dbgserial.h"                                         */
/*    ...                                                                    */
/*    dbg_init(kapi);                                                        */
/*    DBG("hello from serial");                                              */
/*    DBGF("value = %d", 42);                                                */
/* ======================================================================== */

#ifndef __LIBOS32_DBGSERIAL_H
#define __LIBOS32_DBGSERIAL_H

#include "os32api.h"

#ifdef OS32_DBG_SERIAL

/* ======================================================================== */
/*  有効時: KAPIシリアルを利用した実装                                      */
/* ======================================================================== */

/* シリアルポート初期化 (デフォルトボーレート: 38400) */
void dbg_serial_init(KernelAPI *api);

/* シリアルポート初期化 (ボーレート指定) */
void dbg_serial_init_baud(KernelAPI *api, unsigned long baud);

/* 文字列出力 (末尾に \r\n を付加) */
void dbg_serial_puts(const char *s);

/* 文字列出力 (改行なし) */
void dbg_serial_print(const char *s);

/* 1文字出力 */
void dbg_serial_putchar(char ch);

/* printf風フォーマット出力 (末尾に \r\n 付加) */
/* 対応フォーマット: %d %u %x %X %s %c %% %p %ld %lu %lx */
void dbg_serial_printf(const char *fmt, ...);

/* 16進ダンプ (addr, len バイト分を hex+ASCII 表示) */
void dbg_serial_hexdump(const void *addr, int len);

/* 安全なメモリダンプ (256バイト固定, ページテーブル安全チェック付き) */
void dbg_memdump(u32 addr);

/* 安全なメモリダンプ (長さ指定, 最大1024バイト) */
void dbg_memdumpn(u32 addr, int len);

/* ======== マクロ ======== */

/* 単純文字列出力 */
#define DBG(msg)        dbg_serial_puts(msg)

/* printf風出力 */
#define DBGF(fmt, ...)  dbg_serial_printf(fmt, __VA_ARGS__)

/* 安全なメモリダンプ (アドレス指定) */
#define DBG_DUMP(addr)        dbg_memdump((u32)(addr))
#define DBG_DUMPN(addr, len)  dbg_memdumpn((u32)(addr), (len))

/* 初期化 (デフォルトボーレート) */
#define dbg_init(api)           dbg_serial_init(api)
#define dbg_init_baud(api, b)   dbg_serial_init_baud(api, b)

#else /* !OS32_DBG_SERIAL */

/* ======================================================================== */
/*  無効時: 全マクロが空に展開される (ゼロコスト)                           */
/* ======================================================================== */

#define dbg_init(api)           ((void)0)
#define dbg_init_baud(api, b)   ((void)0)
#define DBG(msg)                ((void)0)
#define DBGF(fmt, ...)          ((void)0)
#define DBG_DUMP(addr)          ((void)0)
#define DBG_DUMPN(addr, len)    ((void)0)

/* 関数ポインタとして渡す必要がある場合のダミー inlines は不要 */
/* (C89ではinline非標準のため、マクロのみで完結させる) */

#endif /* OS32_DBG_SERIAL */

#endif /* __LIBOS32_DBGSERIAL_H */
