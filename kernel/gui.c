/* ======================================================================== */
/*  GUI.C — GUI シェル v1.1 のカーネル背骨 (K レーン K1)                     */
/*                                                                          */
/*  gui_call / gui_register / gui_owner_exit / sys_switch_shell を実装する。 */
/*  カーネルは op の意味を知らない: 登録ハンドラへ転送し owner を付けるだけ。 */
/*  WM からアプリへのコールバック経路は作らない (契約 T1: 再入の根絶)。       */
/* ======================================================================== */

#include "gui.h"
#include "kstring.h"
#include "kbd.h"      /* kbd_set_gui_mode (K2-B) */
#include "ime.h"      /* ime_set_render (レビュー #4 ①: gshell 終了時の防御的リセット) */

/* res_owner_get() は fs/fd_redirect.c。FD と同じ「確保した実行レベル」。
 * K5b からは値の意味が **アプリ ID** (1 = シェル帯 / 2〜5 = アプリ) になった。
 * ドライバ/カーネル各所と同じ流儀で extern 宣言する (-Ifs 非依存)。 */
extern int res_owner_get(void);

/* いま処理中の op が OP_WAIT かどうかをカーネルに控えさせる (票 K5 の C4)。
 * exec_park() はこれを見て「OP_WAIT の中からしか譲らない」を強制し、park した
 * フレームに印を立てる。実体は exec/appslot.c (kernel/ は -Iexec 非依存)。 */
extern void appslot_gui_op_enter(int is_wait);
extern void appslot_gui_op_leave(void);
extern i32 exec_trim_done(u32 epoch);

/* 「カーネルが WM のコードへ入っている」印 (exec/exec.c の ring3_wm_depth、
 * 2026-09-26)。WM はアプリの syscall の中で走るので、この印が無いと WM 自身の
 * ポインタ (自分のスタックの MouseInfo 等) が KAPI の出力検査でアプリの番地と
 * して見られ、シェル帯に USER が無いので拒否 → アプリが kill された (filer が
 * 窓も出さずに消えた件)。ハンドラ / owner_exit の前後で必ず対にする。
 * ハンドラが longjmp で戻らないとき (OP_WAIT の中の exec_park、fault kill) は
 * leave を通らないが、exec 側が longjmp の地点とディスパッチャの入口で 0 に
 * 戻す。実体は exec/exec.c (kernel/ は -Iexec 非依存)。
 * 深さ 1 以上の間 CPL=3 が走らないのは、gshell が契約 T1 を守る限り
 * (OWNER_EXIT ハンドラから exec_start / exec_resume を呼ばない)。呼んでも
 * 入れ子の syscall はディスパッチャ入口で深さ 0 に戻るので門の穴にはならない
 * (対の崩れは ring3_wm_depth_underflow が数える)。また深さが素通しにするのは
 * **WM がいま渡すポインタ**だけで、アプリが前に登録したポインタ
 * (fd_redirect のバッファ) は深さに関係なく表を歩く (exec/ring3_str.h)。
 * 深さ 1 以上 = WM を信頼境界の内側に置く、だがフォールトの帰属は
 * ring3_in_syscall のまま — WM の中で落ちるとアプリの kill として畳まれる
 * (ring3_wm_fault_count が数える、docs/POLICY_DEBUG.md §4-61 追記 3)。 */
extern void ring3_wm_enter(void);
extern void ring3_wm_leave(void);

/* 「いまの KAPI 呼び出しは CPL=3 のアプリ由来か」(exec/exec.c、実体は
 * exec/ring3_str.c の ring3_guard_active)。gui_register と gui_ime_set_render
 * が引く (同じ門)。 */
extern int ring3_call_from_user(void);

/* GUI_SHELL_OWNER (= 1) は gui.h。K2 の syscall 境界ポンプも同じ値を使う。 */

/* ======== WM 登録状態 ======== */
/* gui_register で shell 帯から一度だけ登録される。CPL=0 のシェル帯コード
 * だけが書き手なので追加のロックは不要。 */
static GuiHandler g_gui_handler = 0;
static void      *g_gui_pump    = 0;

/* 次に起動するシェルのパス (契約 T9)。空文字列 = 記録なし。 */
static char g_next_shell[OS32_MAX_PATH];

/* ======================================================================== */
/*  gui_call — アプリ → WM の唯一の入口 (契約 T1)                            */
/* ======================================================================== */
i32 gui_call(u32 op, u32 arg)
{
    i32 r;
    if (op == GUI_OP_OWNER_EXIT) return OS32_ERR_INVAL;
    if (op == GUI_OP_TRIM_DONE) return exec_trim_done(arg);
    if (g_gui_handler == 0) {
        return OS32_ERR_NOSYS;
    }
    /* OWNER_EXIT は内部通知専用。それ以外は「いまが OP_WAIT か」を控える
     * — OP_WAIT 内の明示的な park の判定材料
     * (票 K5 の D0/C4)。WM が OP_WAIT の中で exec_park() を呼んだときは
     * longjmp するのでこの関数へは戻ってこない。 */
    appslot_gui_op_enter(op == GUI_OP_WAIT);
    ring3_wm_enter();
    r = g_gui_handler(op, arg, res_owner_get());
    ring3_wm_leave();
    appslot_gui_op_leave();
    return r;
}

/* ======================================================================== */
/*  gui_register — WM (gshell) がハンドラを登録する                          */
/*  shell 帯 (owner 1, CPL=0) からのみ。それ以外は OS32_ERR_INVAL。          */
/*                                                                          */
/*  門は gui_ime_set_render と同じ「owner 1 かつ ring3_call_from_user() が  */
/*  偽」(2026-09-26、代行レビュー P3)。登録したハンドラ / ポンプは以後ずっと */
/*  CPL=0 で呼ばれるので、CPL=3 のアプリの syscall 由来 (WM の外) では断る。 */
/*  owner 1 は常駐シェルだけなので owner だけでも今は穴ではないが、門を     */
/*  1 つの形に揃えて「owner の意味が変わったら開く」を塞ぐ。正当な呼び手は   */
/*  gshell の top-level (ディスパッチの外、ring3_in_syscall = 0) で、これは  */
/*  通る (tools/tests/ring3_guard_host.c §6)。                               */
/* ======================================================================== */
i32 gui_register(void *handler, void *pump)
{
    if (res_owner_get() != GUI_SHELL_OWNER || ring3_call_from_user()) {
        return OS32_ERR_INVAL;
    }
    if (handler == 0) {
        return OS32_ERR_INVAL;
    }
    g_gui_handler = (GuiHandler)handler;
    g_gui_pump    = pump;   /* K2 の syscall 境界ポンプが呼ぶ (exec/exec.c) */

    /* WM が入ったのでキーボードを GUI モードへ (K2-B、W1 申し送り ①)。
     * WM は raw リングだけを読むので、cooked リング (kbd_buf) に積み続けると
     * 32 打鍵で溢れて kbd_dropped が増え、実際には落ちていない打鍵が
     * 偽の OVERFLOW として WM → アプリへ伝わる。GUI 中は積まない。 */
    kbd_set_gui_mode(1);
    return 0;
}

/* ======================================================================== */
/*  gui_owner_exit — 所有者による回収の口 (契約 T4 / U8)                     */
/*                                                                          */
/*  exec_exit() が owner 別回収の並びで呼ぶ。ハンドラが登録済みなら           */
/*  GUI_OP_OWNER_EXIT を渡し、WM がその owner のウィンドウ・サーフェス・       */
/*  タイマ・スロットを回収する (W1 が実装)。未登録なら何もしない。           */
/* ======================================================================== */
void gui_owner_exit(int owner, int kind)
{
    if (g_gui_handler != 0) {
        ring3_wm_enter();
        g_gui_handler(GUI_OP_OWNER_EXIT, (u32)kind, owner);
        ring3_wm_leave();
    }
    /* WM 自身 (gshell = shell 帯 owner 1) が終了するなら登録を解除する。
     * これをしないと、gshell.bin が抜けて同じ 0x300000 に shell.bin が
     * 載った後も g_gui_handler / g_gui_pump が旧コードを指したままになり、
     * K2 の syscall 境界ポンプが別コードを呼んでしまう (レビュー ①)。 */
    if (owner == GUI_SHELL_OWNER) {
        g_gui_handler = 0;
        g_gui_pump    = 0;
        /* CUI に戻るのでキーボードを元に戻す (cooked リングを再開)。
         * これをしないと shell.bin が載っても打鍵が届かない (K2-B)。 */
        kbd_set_gui_mode(0);
        /* FEP の描画先を TVRAM 版へ戻す (レビュー #4 ①)。gshell は自分でも
         * NULL を渡すが、クラッシュで抜けた場合も 0x300000 の旧コードを
         * 指したままにしない。 */
        ime_set_render((void *)0);
    }
}

/* ======================================================================== */
/*  gui_ime_set_render — KAPI ime_set_render の入口 (常駐側だけ)             */
/*                                                                          */
/*  カーネルは控えた IME_Render 表の putc / putw / clear_row を**以後ずっと  */
/*  CPL=0 で**呼ぶ (kernel/ime.c)。アプリが自分のメモリの表を渡せると、その   */
/*  アプリのコードが CPL=0 で走り、別アプリの syscall や WM の top-level      */
/*  (master CR3) でも呼ばれ、アプリが消えた後は解放済み / 他人の物理へ飛ぶ   */
/*  (2026-09-26 代行レビュー P2)。正当な呼び手は gshell の top-level だけ    */
/*  (起動時 / CUI 切替 / 停止、shell 帯 owner 1、CPL=0)。                    */
/*                                                                          */
/*  そこで gui_register / sys_switch_shell と同じ「owner 1 から」に加えて、  */
/*  ring3_call_from_user() が真 (CPL=3 のアプリの syscall、WM の外) なら断る。*/
/*  NULL (TVRAM 版へ戻す) も同じ規則 — アプリが WM の描画先を外すこともさせ */
/*  ない。KAPI の戻りは void なので ([ABI2]、引数も戻りも変えない) 黙って    */
/*  断り、回数だけ gui_ime_render_rejected に数える (kselftest / デバッグ)。 */
/*  カーネル内の後始末 (gui_owner_exit(1) の NULL 戻し) はこの門を通らず      */
/*  ime_set_render を直に呼ぶ。                                              */
/* ======================================================================== */
volatile u32 gui_ime_render_rejected = 0;

void gui_ime_set_render(void *table)
{
    if (res_owner_get() != GUI_SHELL_OWNER || ring3_call_from_user()) {
        gui_ime_render_rejected++;
        return;
    }
    ime_set_render(table);
}

/* ======================================================================== */
/*  gui_get_pump — K2 用: 登録された入力ポンプを返す                         */
/* ======================================================================== */
void *gui_get_pump(void)
{
    return g_gui_pump;
}

/* ======================================================================== */
/*  sys_switch_shell — 次シェルのパスを記録する (契約 T9、K4 の入口)         */
/*                                                                          */
/*  shell 帯 (owner 1) からのみ。実際の入れ替えはカーネルのシェル起動ループ  */
/*  (K4) が gui_next_shell() を読んで行う。呼んだシェルは自分で後始末して     */
/*  exit する。                                                              */
/* ======================================================================== */
i32 sys_switch_shell(const char *path)
{
    if (res_owner_get() != GUI_SHELL_OWNER) {
        return OS32_ERR_INVAL;
    }
    if (path == 0 || path[0] == '\0') {
        return OS32_ERR_INVAL;
    }
    /* 切り詰めて記録しない (票 TASK_VFS_FD_PATH v3 の追記)。切った名前は
     * 別のシェルを起動し得る。上限まで数えて NUL が無ければ断る */
    {
        int n;
        for (n = 0; n < OS32_MAX_PATH && path[n]; n++) { }
        if (n >= OS32_MAX_PATH) return OS32_ERR_NAMETOOLONG;
    }
    kstrncpy(g_next_shell, path, OS32_MAX_PATH);
    g_next_shell[OS32_MAX_PATH - 1] = '\0';
    return 0;
}

/* ======================================================================== */
/*  gui_next_shell — K4 用: 記録された次シェルのパス (無ければ NULL)         */
/* ======================================================================== */
const char *gui_next_shell(void)
{
    if (g_next_shell[0] == '\0') {
        return (const char *)0;
    }
    return g_next_shell;
}

/* ======================================================================== */
/*  gui_take_next_shell — K4 用: 要求を取り出して消す (consume 方式)         */
/*                                                                          */
/*  起動ループはこちらを使う。パス比較で「前回と違うときだけ新要求」と        */
/*  みなす方式だと、gshell のロード失敗や連続 fault で CUI に落ちた後に       */
/*  同じ /bin/gshell.bin を要求しても二度と採用されない (レビュー #3 ④)。    */
/*  戻り値: 要求があれば 1 (out にコピー)、無ければ 0。                       */
/* ======================================================================== */
int gui_take_next_shell(char *out, int cap)
{
    if (g_next_shell[0] == '\0' || out == 0 || cap <= 0) {
        return 0;
    }
    kstrncpy(out, g_next_shell, cap);
    out[cap - 1] = '\0';
    g_next_shell[0] = '\0';
    return 1;
}

/* ======================================================================== */
/*  gfx_stats / gfx_lease_palette — H1 の依頼分の weak 既定                  */
/*                                                                          */
/*  本実装は H レーンが gfx/ (gfx_core.c) に置く (票 §3「H1 からの依頼」)。  */
/*  K1 は KAPI スロットを切るだけだが、H1 未着手でもカーネルがリンクできる   */
/*  よう weak な既定を置く。H1 の強シンボルがリンク時に上書きする。          */
/*  TODO(H1): gfx/ で本実装に置き換える。                                    */
/* ======================================================================== */
__attribute__((weak)) int gfx_stats(void *out)
{
    if (out != 0) {
        kmemset(out, 0, sizeof(GFX_Stats));
    }
    return OS32_ERR_NOSYS;
}

__attribute__((weak)) int gfx_lease_palette(int first, int count, const u8 *rgb)
{
    (void)first;
    (void)count;
    (void)rgb;
    return OS32_ERR_NOSYS;
}
