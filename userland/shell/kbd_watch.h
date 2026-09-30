/* ======================================================================== */
/*  KBD_WATCH.H — `kbdstat -w` の行の組み立て (純粋部)                       */
/*                                                                          */
/*  KAPI も I/O も呼ばない。cmd_sys.c の kbdstat -w が kbd_diag_log         */
/*  (KAPI v67) で受け取ったエントリをここで 1 行にし、kprintf で出す。       */
/*  ホスト試験 (tools/tests/test_kbd_dlog.py) が実物を #include して         */
/*  書式を固定する。票 docs/tasks/gui/TASK_KBD_NAV.md §3。                   */
/* ======================================================================== */

#ifndef __KBD_WATCH_H
#define __KBD_WATCH_H

#include "os32_kapi_shared.h"   /* KbdDiagLogEnt / KBD_DLOG_* */

/* 1 行のバッファ長 (NUL 込み)。いちばん長い行でも収まる大きさ。 */
#define KBDW_LINE_MAX       96
/* 何秒で打ち切るか。tick は PIT_HZ (include/memmap.h、100Hz) — シェルは
 * memmap.h を引かないので写しを置く (get_tick() / 100 が秒の既存の流儀)。 */
#define KBDW_TIMEOUT_SEC    30
#define KBDW_TICK_HZ        100
#define KBDW_KEY_ESC        0x1B   /* 終了キー (cooked の ASCII) */

/* 修飾を "SHIFT|CAPS|KANA|GRPH|CTRL" の部分列で書く (無ければ "-")。
 * 戻り = 書いた長さ (NUL を除く)。cap が足りなければ切り詰める (必ず NUL 終端)。 */
int kbdw_fmt_mods(char *buf, int cap, u32 mods);

/* 1 件の行 (改行なし):
 *   "seq=12 code=F2 break key=72 KANA mods=CAPS|KANA [OE] [V86] [GUI]"
 * code は 0041h の生の値 (16 進 2 桁)、make / break は bit7、key は下位 7 ビット。
 * key の名前は修飾キーと ESC だけ付ける。[..] はフラグが立っているときだけ。 */
int kbdw_fmt_ent(char *buf, int cap, const KbdDiagLogEnt *e);

/* 取りこぼしの件数: 最後に出した seq が prev、今回の先頭が first のとき
 * first - prev - 1 (first <= prev + 1 なら 0)。 */
u32 kbdw_lost(u32 prev, u32 first);

/* 取りこぼしの行 (改行なし):
 *   "LOST seq=13..20 (8): cannot judge this span"
 * (取りこぼし — この区間は判定不能)。lost が 0 なら空文字列で 0 を返す。 */
int kbdw_fmt_lost(char *buf, int cap, u32 prev, u32 first);

#endif /* __KBD_WATCH_H */
