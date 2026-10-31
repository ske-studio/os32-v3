/* Abort requests, safe-point checks and GUI input pumping. Included by exec.c. */
/* ======================================================================== */
/*  GUI 入力ポンプと強制脱出 (K2 / 契約 T6・T8 の X4)                        */
/* ======================================================================== */

/* IRQ0 が 100Hz で加算するティック (arch/x86/kernel/idt.h)。ポンプの上限判定は
 * これを **読むだけ** で行う (票 K2 の鉄則: get_tick を叩く回数を増やさない)。 */
extern volatile u32 tick_count;

/* ポンプ実行中フラグ (票 K2-3 の再入防止)。ポンプ自身は CPL=0 の WM コード
 * なので int 0x80 は経由しないが、将来ポンプの中から何かが syscall 境界を
 * 通っても二重取り込みにならないようにカーネル側でも止める。 */
static volatile int g_gui_pump_busy = 0;

/* 最後にポンプを回した tick と、その値が有効かどうか。
 * 「同じ tick の間は 1 回だけ」= 前回から 1 tick (10ms) 未満なら呼ばない。 */
static u32 g_gui_pump_tick = 0;
static int g_gui_pump_tick_valid = 0;

/* CPL=3 アプリの強制脱出要求 (CTRL+STOP、契約 T6)。IRQ1 の kbd_irq_handler が
 * 立て、(a) IRQ1 スタブ (割り込まれた文脈が CPL=3 = アプリのコード実行中の
 * とき) と (b) syscall 入口 が見て ring3_fault_kill する。カーネル内 (wrap の
 * 実行中) では畳まない — 中途半端なカーネル状態で longjmp しないため、
 * 要求は残して次の安全な地点で処理する。
 *
 * K5b: 要求は **走っているアプリの AppSlot** に立てる (D4)。IRQ1 の時点で
 * カーネルが知っているのはそれだけで、止めてあるアプリには届かない
 * (そちらは WM が exec_kill で畳む)。 */

/* CTRL+STOP で畳んだ回数 (PM の V4 検証が emu_read_mem で読む)。
 * fault_kill_count にも含まれる (畳む経路は同じ ring3_fault_kill)。 */
volatile u32 ring3_abort_count = 0;

/* ======================================================================== */
/*  ring3_abort_request — CTRL+STOP を受けた (IRQ1 の ISR から呼ばれる)      */
/*                                                                          */
/*  ISR の中では畳まない (EOI も V86 反射も済んでいない)。CPL=3 アプリが      */
/*  走っているときだけ要求を立てる。CUI でシェルしか居ないときは無視。        */
/* ======================================================================== */
void ring3_abort_request(void)
{
    /* 票 T9 §12 S6: GUI 中は宛先 (フォーカス窓の連鎖の末尾、D8) を知って
     * いるのは WM だけなので、カーネルは立てない — 要求は raw リング経由で
     * WM へ届き、WM が exec_abort_clear → exec_kill(末尾) を実行する
     * (決裁 A1)。立てると「そのとき走っていた slot」= WAIT_POLL の sh や
     * 100ms タイマの端末が巻き込まれる (受入 S6 の 2 回目)。
     * 例外は暴走 (APP_RUNAWAY_TICKS 以上 WM へ戻っていない) だけ。
     * CUI 中は 1 バイトも変えない (K2 の唯一の逃げ道)。 */
    if (!appslot_abort_admit(con_sink_is_enabled(), tick_count)) {
        appslot_stop_request();
        return;
    }
    appslot_abort_request();
}

/* Quarantine notification also targets parked apps. Like IRQ1's request this
 * only stores abort_req; it never reclaims or longjmps with IRQs disabled.
 * Four slots bound the scan even inside paging_lease_unmap's IRQ section. */
void exec_addrspace_abort(struct addrspace *as)
{
    for (int id = APP_ID_MIN; id <= APP_ID_MAX; id++) {
        AppSlot *a = appslot_get(id);
        if (a && a->as == as) a->abort_req = 1;
    }
}

/* Only explicit KAPI waits use this probe. It never unwinds kernel state.
 * A gui=0 nested child cannot hand STOP to the WM at syscall exit. */
int ring3_wait_pending(void)
{
    AppSlot *a = appslot_get(appslot_cur());
    if (!ring3_call_from_user() || !a) return 0;
    return a->abort_req || (a->gui && a->cpl3 && appslot_stop_pending());
}

/* ======================================================================== */
/*  ring3_abort_check — 要求があればアプリを畳む (戻らない)                  */
/*                                                                          */
/*  呼び出し元:                                                             */
/*    - arch/x86/kernel/isr_stub.asm の IRQ1 スタブ (EOI と V86 反射の後、割り込まれた */
/*      文脈が CPL=3 のときだけ)。KAPI を呼ばない計算ループはここで死ぬ。     */
/*    - ring3_syscall_dispatch の入口 (wrap に入る前)。                      */
/*    - 公開 KAPI の待ち (内部経路を除き、資源の後始末が済んだ安全点)。     */
/*  IRQ 上では資源を触らず移譲し、launch/resume の着地点で IF=1 にして回収。*/
/*  syscall 入口の通常の安全点では共通回収へ直行する (T2a R1)。            */
/* ======================================================================== */
void ring3_abort_check(void)
{
    if (font_guard_active) return;
    AppSlot *a = appslot_get(appslot_cur());
    if (!a || !a->abort_req) return;
    a->abort_req = 0;
    if (!g_cur_app) return;         /* CPL=3 アプリはもう居ない */
    ring3_abort_count++;
    ring3_abort_kill();             /* 戻らない */
}

/* ======================================================================== */
/*  ring3_gui_pump — syscall 境界の入力ポンプ (票 K2-1/2/3、契約 T6)         */
/*                                                                          */
/*  アプリが KAPI を呼んでいる限り、WM (gshell) が登録したポンプをここで      */
/*  回し、キーボードとマウスの入力を取りこぼさないようにする。               */
/*                                                                          */
/*  上限 (票 K2-1):                                                          */
/*    - syscall 1 回につき最大 1 回 (この関数はディスパッチャから 1 回だけ    */
/*      呼ばれる)。                                                          */
/*    - 前回から 1 tick (10ms) 未満なら呼ばない。KAPI を毎秒数百回叩く        */
/*      アプリ (v2 PLAN §3 の実測 233/s) で WM の処理が支配的にならないため。 */
/*                                                                          */
/*  除外 (票 K2-3): WM 自身 (owner 1 = シェル帯) からの呼び出し。WM は自分の  */
/*  周期 (X3 = OP_WAIT) で回すので、ここで二重に回さない。                    */
/*                                                                          */
/*  呼ぶ位置は kapi_invoke の **前**、かつフォールトガード                    */
/*  (ring3_in_syscall) の **外側**。ポンプは CPL=0 の WM コードで、ここで      */
/*  落ちるのはアプリのせいではない (契約 T8 の X4: 落ちたらカーネルの責任)。  */
/*  ポンプ実行中も IF=1 のまま — IRQ は普通に入る (int80_stub が sti 済み)。  */
/* ======================================================================== */
static void ring3_gui_pump(void)
{
    GuiPump pump;
    u32 now;

    if (g_gui_pump_busy) return;             /* 再入防止 (票 K2-3) */

    pump = (GuiPump)gui_get_pump();
    if (pump == 0) return;                   /* WM 未登録 (CUI) */

    if (res_owner_get() == GUI_SHELL_OWNER) return;  /* WM 自身の syscall */

    now = tick_count;
    if (g_gui_pump_tick_valid && now == g_gui_pump_tick) {
        return;                              /* 同じ tick 内 = 1 tick 未満 */
    }
    g_gui_pump_tick = now;
    g_gui_pump_tick_valid = 1;

    g_gui_pump_busy = 1;
    /* ここは ring3_in_syscall = 0 の区間だが、WM の文脈である印は同じ形で
     * 立てる (ポンプの位置が動いても 3 つの門の判定が変わらないように)。 */
    ring3_wm_enter();
#ifdef OS32_R1_FIXTURE
    font_guard_fixture();
#endif
    pump();
    ring3_wm_leave();
    g_gui_pump_busy = 0;
}
