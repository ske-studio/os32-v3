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
/*  通常の注入権は con_sink の読み手 (= 端末)。全画面 owner が端末の子孫    */
/*  でない間だけ WM 本人にも許可し、読み手無しの Run 起動を扱う。その他は   */
/*  OS32_ERR_EXIST。起動要求表と slot の親子関係を使い、所有者表は増やさない。*/
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
 *   通常はcon_sinkの読み手だけ。冒頭の全画面WM例外にも該当しなければ
 *   OS32_ERR_EXIST (票 §5 の R2、e11aの例外)。 */
i32 kbd_inject(const u8 *utf8, u32 len);
/* kbd_inject_pending: 現在のアプリが取得可能な未読バイト数。
 * 端末由来は従来どおり。WM由来は注入時の全画面ownerとその子孫だけ。
 * WM自身からは端末由来だけが見える。 */
u32 kbd_inject_pending(void);

/* --- カーネル内から (drivers/kbd.c, exec/exec.c) ------------------------- */
/* 現在のアプリ宛てを1バイト取り出す。取れたら1、空なら0 (*out不変)。
 * 取得できない宛先は飛ばし、宛先ごとのFIFO順序を維持する。 */
int kbd_inject_take(u8 *out);
/* exec_resume はまだWM文脈なので、検証済み再開先を明示する。KAPI非公開。 */
int kbd_inject_take_for(int id, u8 *out);
/* 1 バイト**覗く** (取り出さない)。覗けたら 1 を返して *out に書く。空なら 0。
 * kbd_peekkey (drivers/kbd.c) 用 — GUI 中の「ESC か否かだけ見て、ESC でなければ
 * 次の読み手に残す」を、リングを 1 バイトも動かさずに書くために要る。 */
int kbd_inject_peek(u8 *out);
/* 溜まっているものを捨てる。CUI 復帰 (console_text_gdc_start) から。 */
void kbd_inject_discard(void);
/* 退場ID宛てのWM注入だけを捨てる。con_sink読み手の退場なら端末由来も
 * 捨てる (exec_reclaim_owned から con_sink_owner_exit の直前に呼ぶ)。 */
void kbd_inject_owner_exit(int id);

/* --- 自己診断 (kernel/kselftest.c) --------------------------------------- */
/* リングの push/take/破棄/あふれと「読み手未確立の注入は拒否」を確かめる。
 * ビット 0..n が落ちた項目 (0 = 全部通った)。呼んだ後のリングは空。 */
u32 kbd_inject_selftest(void);

#endif /* __KBD_INJECT_H */
