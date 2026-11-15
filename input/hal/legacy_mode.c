#include "in2_int.h"
#include "io.h"
#include "../../drivers/input/kbd.h"

extern void appslot_poll_yield_reset(void);
extern volatile int kbd_stop_suppressed;

/* GUI モード (W1 申し送り ①、K2-B)。WM (gshell) が gui_register した間だけ 1。
 *
 * WM は raw リングだけを読む。cooked リング (kbd_buf) は誰も読まないので、
 * 積み続けると 32 打鍵で満杯になり kbd_dropped が増え続ける。それを
 * kbd_dropped_count() 経由で WM が拾うと、実際には 1 打鍵も落ちていないのに
 * ヘッダの dropped が増えて OVERFLOW が立つ (偽の取りこぼし)。
 * したがって GUI 中は cooked に積まない — 打鍵の勘定を raw 側 1 本にする。
 * kbd_dropped_count() の意味 (契約 T3: WM が差分を dropped に足す) は
 * 変わらない。ASCII 化は WM が自前の写し (gshell の input::translate) で行う。
 * rshell のシリアル入力は kbd_count を経由しないので影響を受けない。 */
volatile int kbd_gui_mode = 0;

/* ======================================================================== */
/*  kbd_set_gui_mode — GUI (WM 常駐) モードの切替 (K2-B、W1 申し送り ①)     */
/*                                                                          */
/*  kernel/gui.c が gui_register で 1、gui_owner_exit(owner 1) で 0 にする。 */
/*  KAPI は増やさない (GUI の状態はカーネル内で完結する)。                    */
/*  切替のたびに cooked リングを空にする:                                     */
/*    - GUI へ入るとき: CUI で溜まっていた打鍵を捨てる (WM は raw から同じ    */
/*      打鍵を既に受け取っているので二重にはしない)。                        */
/*    - CUI へ戻るとき: GUI 中に (この関数が 0 にする前に) 積まれた分を       */
/*      シェルのプロンプトへ流し込まない。                                    */
/*  捨てた分は kbd_dropped に数えない — 経路の切替であって取りこぼしでは      */
/*  ないため (数えると WM 側に偽の OVERFLOW が出る)。                        */
/*  head/tail/count は IRQ1 と競合するので irq_save で囲む。                  */
/* ======================================================================== */
void kbd_set_gui_mode(int on)
{
    int next = on ? 1 : 0;
    unsigned int flags;

    if (next == kbd_gui_mode) return;

    flags = irq_save();
    kbd_gui_mode = next;
    kbd_stop_suppressed = 0;
    if (next) {
        /* GUI セッションの起点で取りこぼし累計を 0 に戻す (レビュー #4 ③)。
         * gshell は last_kbd_dropped=0 から差分を取るので、前回セッションや
         * CUI 中の cooked drop が最初の取り込みで偽の OVERFLOW になっていた。 */
        legacy_cooked_reset_dropped();
        legacy_raw_reset_dropped();
    }
    legacy_cooked_flush();
    /* raw リングも空にする。raw は WM しか読まないので、CUI 中の打鍵 (rshell を
     * 閉じた ESC 等) が残ったまま次の gshell に届き、起動直後に ESC で終了して
     * しまう (2026-09-06 実測: os32gui の直後に CUI へ戻る)。GUI → CUI の
     * 向きも同様に、次回の gshell へ古い打鍵を持ち越さない。 */
    legacy_raw_flush();
    irq_restore(flags);
    /* ポーリング型 yield の間引き (票 T8 §7 D8) もセッションの境界で戻す。
     * 錠の外で呼ぶ — 触るのは exec/appslot.c の 1 語で、IRQ1 は見ない。 */
    appslot_poll_yield_reset();
}

