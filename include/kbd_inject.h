/* ======================================================================== */
/*  KBD_INJECT.H — 打鍵の注入リング (GUI モード中の kbd_getchar を満たす)   */
/*                                                                          */
/*  票: docs/archive/gui_v13/TASK_K7_input.md §1 D2 / §5 (K7-K = カーネル +    */
/*      KAPI v47)                                                           */
/*                                                                          */
/*  GUI モード中 IRQ1 は raw リングにしか積まない (drivers/kbd.c の          */
/*  kbd_gui_mode の説明)。打鍵を受け取るのは WM (gshell) で、そこから        */
/*  フォーカス窓の端末アプリへ GUI_EV_KEY / GUI_EV_TEXT として配られる。     */
/*  端末アプリはそれを **UTF-8 のバイト列のまま**ここへ注入し、GUI 中の      */
/*  kbd_getchar / kbd_getkey はこのリングだけを見る。                        */
/*                                                                          */
/*  注入できるのは **con_sink の読み手** (= その端末アプリ) だけ。読み手が   */
/*  居ない / 別の所有者からは OS32_ERR_EXIST。con_sink と同じ「1 本だけ」の  */
/*  所有規則をそのまま借りるので、権限の表をもう 1 つ持たなくてよい。        */
/*                                                                          */
/*  容量は 256B (票 §1 メモリ)。あふれたら**新しい方を捨てる** — con_sink    */
/*  (古い方を捨てる) と逆なのは、打鍵は順序が意味を持つのに対し、古い方を    */
/*  捨てると「打った文字列の先頭が消える」形になるため。捨てた分は戻り値の   */
/*  差 (積んだバイト数 < len) で呼び手に伝わる。                             */
/* ======================================================================== */

#ifndef __KBD_INJECT_H
#define __KBD_INJECT_H

#include "types.h"

/* リング容量 (カーネル帯の静的配列、kmalloc しない)。票 §1 メモリ。 */
#define KBD_INJECT_RING_SIZE   256

/* --- KAPI v47 の実体 ---------------------------------------------------- */
/* kbd_inject: UTF-8 バイト列をリングへ積む。戻り値 = 積んだバイト数
 *   (0 = 1 バイトも入らなかった / len が 0)。utf8 == NULL は OS32_ERR_INVAL。
 *   呼べるのは con_sink の読み手だけで、読み手が未確立 (端末アプリが
 *   con_sink_read を 1 回も呼んでいない) か別の所有者なら OS32_ERR_EXIST
 *   (票 §5 の R2)。 */
i32 kbd_inject(const u8 *utf8, u32 len);
/* kbd_inject_pending: 未読バイト数。所有権は要らない (WM が ready_to_run の
 * 判定に使う。票 §5 の指摘 C)。 */
u32 kbd_inject_pending(void);

/* --- カーネル内から (drivers/kbd.c, exec/exec.c) ------------------------- */
/* 1 バイト取り出す。取れたら 1 を返して *out に書く。空なら 0 (*out 不変)。 */
int kbd_inject_take(u8 *out);
/* 1 バイト**覗く** (取り出さない)。覗けたら 1 を返して *out に書く。空なら 0。
 * kbd_peekkey (drivers/kbd.c) 用 — GUI 中の「ESC か否かだけ見て、ESC でなければ
 * 次の読み手に残す」を、リングを 1 バイトも動かさずに書くために要る。 */
int kbd_inject_peek(u8 *out);
/* 溜まっているものを捨てる。CUI 復帰 (console_text_gdc_start) から。 */
void kbd_inject_discard(void);
/* 読み手が退場したら捨てる (exec_reclaim_owned から、con_sink_owner_exit の
 * **直前**に呼ぶこと — g_reader がまだ生きている間に照合するため)。
 * 注ぎ手が居なくなった以上、残りは誰も足せない古い打鍵にしかならない。 */
void kbd_inject_owner_exit(int id);

/* --- 自己診断 (kernel/kselftest.c) --------------------------------------- */
/* リングの push/take/破棄/あふれと「読み手未確立の注入は拒否」を確かめる。
 * ビット 0..n が落ちた項目 (0 = 全部通った)。呼んだ後のリングは空。 */
u32 kbd_inject_selftest(void);

#endif /* __KBD_INJECT_H */
