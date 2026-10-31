/* Cooperative park, resume and parked-application termination. Included by exec.c. */
/* ======================================================================== */
/*  exec_park — 走っているアプリを OP_WAIT の中で止め、WM へ戻す (KAPI v44)  */
/*                                                                          */
/*  成立すれば **戻らない** (longjmp で exec_start / exec_resume の復帰点へ)。*/
/*  呼べない文脈 (OP_WAIT 以外の op / 走っているアプリが居ない / CPL=0 の子) */
/*  では OS32_ERR_INVAL を返して普通に戻り、ring3_park_reject_count が増える。*/
/*                                                                          */
/*  park 規約 (D2): 呼んでよいのは gshell の op_wait のループ先頭、wm_cycle  */
/*  が 1 周を終えた直後・ring::pending を読む前だけ。ここから longjmp する    */
/*  ので、WM が書きかけの状態を持っていると宙に浮く。                        */
/* ======================================================================== */
i32 exec_park(void)
{
    if (font_guard_active) return OS32_ERR_BUSY;
    if (v86_call_pinned()) return OS32_ERR_BUSY;
    int id = appslot_cur();
    AppSlot *a;
    u32 k;
    int rc;

    rc = appslot_park_check();
    if (rc < 0) return rc;

    a = appslot_get(id);
    /* CPL=3 のフレームが無ければ止めようがない (CPL=0 の子 / 呼び出し文脈が
     * syscall の外)。check を通っていても最後にここで弾く。 */
    if (!a || !g_cur_app || g_cur_app != a || g_cur_frame == 0) {
        ring3_park_reject_count++;
        return OS32_ERR_INVAL;
    }

    /* CPL=3 の続き = int80_stub のフレーム 13 語。resume はこれを積み直して
     * popad; iretd するだけ (D2 の (b): 追加 RAM は 1 アプリ 52B)。 */
    for (k = 0; k < APP_FRAME_WORDS; k++) a->frame[k] = g_cur_frame[k];

    exec_heap_save_state(&a->exec_heap_used);
    ring3_context_clear();     /* この syscall はここで終わる — WM の出口も通らない */

    /* master へ戻してから状態を切り替える (WM は master の下で走る)。 */
    paging_load_cr3(paging_kernel_pd_phys());
    appslot_park_commit();      /* PARKED + 印 + owner 1 へ */
    exec_restore_context(APP_ID_SHELL);

    g_longjmp_reason = EXEC_LJ_PARK;
    g_longjmp_id = id;
    exec_longjmp(a->jmpbuf);    /* 戻らない */
    return 0;
}

/* ======================================================================== */
/*  exec_park_kbd — GUI 中の kbd 待ちで止める (第 2 の park 点、票 K7 D1)    */
/*                                                                          */
/*  drivers/input/kbd.c の kbd_getchar / kbd_getkey が、GUI モードで注入リングが    */
/*  空のときに呼ぶ。成立すれば **戻らない** (exec_park と同じ longjmp)。      */
/*  起こすのは WM で、そのとき exec_resume が注入リングの 1 バイトを EAX へ   */
/*  入れるので、アプリからは kbd_getchar() が普通に値を返したように見える。   */
/*                                                                          */
/*  戻り値 0 = park できなかった。呼び手は従来の `hlt` 待ちへ落ちる。         */
/*  「できなかった」の大半は CPL=0 の呼び手 (常駐シェル) や syscall の外で、  */
/*  これは異常ではないので数えない (票 §5 R1 の条件)。数えるのは CPL=3 の     */
/*  フレームを持ちながら表の側で弾かれた場合だけ                             */
/*  (= CUI の入れ子 exec_run の子。ring3_park_reject_count)。                */
/* ======================================================================== */
int exec_park_kbd(void)
{
    if (font_guard_active) return 0;
    int id;
    AppSlot *a;
    u32 k;

    /* 票 §5 R1: CPL=3 のアプリが syscall の中に居るときだけ止められる。 */
    if (!g_cur_app || !g_cur_app->cpl3 || g_cur_frame == 0) return 0;

    id = appslot_cur();
    if (appslot_park_kbd_check() < 0) return 0;

    a = appslot_get(id);
    if (!a || g_cur_app != a) {
        ring3_park_reject_count++;
        return 0;
    }

    /* 以降は exec_park と 1 行も変えない (D2 の (b): フレーム 13 語を写して
     * master へ戻り、WM の待っている復帰点へ longjmp する)。 */
    for (k = 0; k < APP_FRAME_WORDS; k++) a->frame[k] = g_cur_frame[k];

    exec_heap_save_state(&a->exec_heap_used);
    ring3_context_clear();     /* この syscall はここで終わる — WM の出口も通らない */

    paging_load_cr3(paging_kernel_pd_phys());
    appslot_park_kbd_commit();  /* WAIT_KEY + 印 + owner 1 へ */
    exec_restore_context(APP_ID_SHELL);

    g_longjmp_reason = EXEC_LJ_PARK;
    g_longjmp_id = id;
    exec_longjmp(a->jmpbuf);    /* 戻らない */
    return 0;
}

/* ======================================================================== */
/*  exec_park_poll — ポーリング型の協調 yield (第 3 の park 点、票 T8 §7 D8) */
/*                                                                          */
/*  drivers/input/kbd.c の kbd_trygetchar / kbd_trygetkey が、GUI モードで注入     */
/*  リングが空のときに呼ぶ。**1 周だけ** WM へ譲る park で、手順は           */
/*  exec_park_kbd と 1 行も違わない (フレーム 13 語を写して master へ戻り、   */
/*  WM の待っている復帰点へ longjmp する)。違うのは 2 つだけ:                */
/*                                                                          */
/*    - 印 / 状態が parked_from_poll / WAIT_POLL であること                  */
/*    - **PIT tick の間引き** (10ms に 1 回まで) が掛かること。描画ループの   */
/*      busy-wait から秒間数万回来るので、間引きが無いと譲りだけで CPU を    */
/*      食う。tick は呼び手 (drivers/input/kbd.c が tick_count を読む) から渡す。   */
/*                                                                          */
/*  起こすのは WM で、そのとき exec_resume は注入リングに文字があればそれを、 */
/*  空なら **-1 (キーなし)** を EAX に入れる。アプリからは                    */
/*  kbd_trygetchar() が普通に戻ったように見える。                            */
/*                                                                          */
/*  戻り値 0 = 譲れなかった。呼び手はそのまま -1 を返す (従来どおり)。        */
/*  「譲れなかった」の大半は間引きと CPL=0 の呼び手で、どちらも異常では      */
/*  ないので数えない (数えるのは appslot 側の表の検査で弾かれた分だけ)。      */
/* ======================================================================== */
int exec_park_poll(u32 now_tick)
{
    if (font_guard_active) return 0;
    int id;
    AppSlot *a;
    u32 k;

    /* R1 (exec_park_kbd と同じ): CPL=3 のアプリが syscall の中に居るときだけ。
     * ここは数えない — 間引きより前に置いて、CPL=0 の呼び手 (常駐シェル) が
     * tick の枠を食わないようにする。 */
    if (!g_cur_app || !g_cur_app->cpl3 || g_cur_frame == 0) return 0;

    id = appslot_cur();
    /* 間引き (tick) → 表の検査。順番は appslot_park_poll_check の中で固定。 */
    if (appslot_park_poll_check(now_tick) < 0) return 0;

    a = appslot_get(id);
    if (!a || g_cur_app != a) {
        ring3_park_reject_count++;
        return 0;
    }

    for (k = 0; k < APP_FRAME_WORDS; k++) a->frame[k] = g_cur_frame[k];

    exec_heap_save_state(&a->exec_heap_used);
    ring3_context_clear();     /* この syscall はここで終わる — WM の出口も通らない */

    paging_load_cr3(paging_kernel_pd_phys());
    appslot_park_poll_commit();          /* WAIT_POLL + 印 + owner 1 へ */
    exec_restore_context(APP_ID_SHELL);

    g_longjmp_reason = EXEC_LJ_PARK;
    g_longjmp_id = id;
    exec_longjmp(a->jmpbuf);    /* 戻らない */
    return 0;
}

/* ======================================================================== */
/*  exec_sys_yield — 明示的な譲り (第 4 の park 点、KAPI v49 sys_yield、D5)  */
/*                                                                          */
/*  「いま譲る」と書いた呼び手 (sh の sh_launch が launch_poll の合間に 1 回  */
/*  ずつ呼ぶ) のための park 点。exec_park_poll と手順は 1 行も違わないが、    */
/*  違うのは 2 つ:                                                          */
/*                                                                          */
/*    - **PIT tick の間引きを掛けない**。間引きは描画ループの busy-wait 用で、*/
/*      明示的な譲りに掛けると、同じ tick の中で sh が回り続けて子が走れない。*/
/*    - 印が parked_from_yield で、起こすとき exec_resume は **注入リングを   */
/*      読まず** EAX = 0 を入れる。読むと、sh が譲っている間に届いた子宛の    */
/*      1 バイトを sh が吸って捨てる (票 §6 blocker 1)。                     */
/*                                                                          */
/*  状態は WAIT_POLL のまま (WM から見た起こし方の規則を増やさない)。        */
/*  GUI 中は **必ず** park する (tick 制限なし)。park できない文脈            */
/*  (CUI / CPL=0 / syscall の外 / 入れ子 exec_run の子) では `hlt` 1 回して    */
/*  0 で戻る — 呼び手から見れば「譲った」で同じ。                             */
/* ======================================================================== */
i32 exec_sys_yield(void)
{
    if (font_guard_active) return OS32_ERR_BUSY;
    int id;
    AppSlot *a;
    u32 k;

    /* CUI 中は協調型の相手 (WM) が居ない。従来どおり 1 回 hlt して戻る。 */
    if (!con_sink_is_enabled()) { _halt(); return 0; }

    /* R1 (exec_park_kbd / exec_park_poll と同じ): CPL=3 のアプリが syscall の
     * 中に居るときだけ。CPL=0 の呼び手はここで落ちる。 */
    if (!g_cur_app || !g_cur_app->cpl3 || g_cur_frame == 0) { _halt(); return 0; }

    id = appslot_cur();
    if (appslot_park_yield_check() < 0) { _halt(); return 0; }

    a = appslot_get(id);
    if (!a || g_cur_app != a) {
        ring3_park_reject_count++;
        _halt();
        return 0;
    }

    for (k = 0; k < APP_FRAME_WORDS; k++) a->frame[k] = g_cur_frame[k];

    exec_heap_save_state(&a->exec_heap_used);
    ring3_context_clear();     /* この syscall はここで終わる — WM の出口も通らない */

    paging_load_cr3(paging_kernel_pd_phys());
    appslot_park_yield_commit();         /* WAIT_POLL + 印 + owner 1 へ */
    exec_restore_context(APP_ID_SHELL);

    g_longjmp_reason = EXEC_LJ_PARK;
    g_longjmp_id = id;
    exec_longjmp(a->jmpbuf);    /* 戻らない */
    return 0;
}

/* ======================================================================== */
/*  exec_resume — 止めてあるアプリを 1 本だけ起こす (KAPI v44)               */
/*                                                                          */
/*  戻り値: app_id = また park した / 0 = 終了した / <0 = 起こせなかった      */
/*  wait_ret は OP_WAIT の戻り値 (契約 T3: ring::pending)。保存フレームの     */
/*  EAX スロットに書くので、アプリから見れば gui_call(OP_WAIT) が普通に        */
/*  その値を返したように見える。                                             */
/*                                                                          */
/*  起こせるのは **OP_WAIT で park された印のあるフレームだけ** (C5/C6)。     */
/*  印が無ければ OS32_ERR_STALE を返して ring3_resume_bad_frame_count を上げる*/
/*  — WM の行儀を信じるのではなくカーネルが弾く形 (受入 G7)。                 */
/* ======================================================================== */
i32 exec_resume(i32 app_id, i32 wait_ret)
{
    if (font_guard_active) return OS32_ERR_BUSY;
    if (paging_v86_session_open()) return OS32_ERR_BUSY;
    if (v86_call_pinned()) return OS32_ERR_BUSY;
    AppSlot *a;
    int rc;
    int src;
    u8 ch;

    rc = appslot_resume_check((int)app_id);
    if (rc < 0) return rc;

    a = appslot_get((int)app_id);
    if (!a->cpl3 || !a->as->pd_phys) return OS32_ERR_INVAL;
    src = appslot_resume_source((int)app_id);
    if (src == APP_RESUME_SRC_KEEP) {
        /* STOP parked a completed syscall; do not overwrite EAX or read keys. */
    } else if (src == APP_RESUME_SRC_YIELD) {
        /* 票 T9 D5: 明示的な譲り (sys_yield) は **注入リングを読まない**。
         * 読むと、sh が譲っている間に届いた子宛の 1 バイトを吸って捨てる
         * (票 §6 blocker 1)。アプリからは sys_yield() が 0 を返して見える。 */
        a->frame[APP_FRAME_EAX] = 0;
    } else if (src == APP_RESUME_SRC_POLL) {
        /* 票 T8 §7 D8: ポーリング型は **1 周だけ**の譲りなので、注入リングが
         * 空でも起こす (WAIT_KEY と違って OS32_ERR_AGAIN を返さない)。
         * 空なら EAX = -1 = 「キーなし」で、アプリの kbd_trygetchar() は
         * 普通に -1 を返したように見える。WM が渡した wait_ret は使わない。 */
        ch = 0;
        if (kbd_inject_take_for((int)app_id, &ch)) a->frame[APP_FRAME_EAX] = (u32)ch;
        else                      a->frame[APP_FRAME_EAX] = (u32)(i32)-1;
    } else if (src == APP_RESUME_SRC_KBD) {
        /* 票 §5 の指摘 B: 文字の取り出しはここで完結する (WM 側に取り出し用
         * の KAPI は作らない)。WM が渡した wait_ret は**使わない**。
         * 端末由来も読み手とslot/launchの子孫だけが取得できる (GUIRUNKEY-1)。
         * WMのpendingはヒントなので、別のRunのWAIT_KEYに端末入力を渡さない。
         * 空なら起こさず OS32_ERR_AGAIN — 印も状態も残るので、WM は次の周で
         * もう一度試せばよい (その周は譲る = streak に数えない)。 */
        ch = 0;
        if (!kbd_inject_take_for((int)app_id, &ch)) return OS32_ERR_AGAIN;
        a->frame[APP_FRAME_EAX] = (u32)ch;
    } else {
        a->frame[APP_FRAME_EAX] = (u32)wait_ret;
    }

    volatile Ring3CallContext caller_context;
    ring3_context_save(&caller_context);
    if (exec_setjmp(a->jmpbuf) != 0) {
        ring3_context_clear();
        /* park / 終了 / fault / kill で戻ってきた。ローカルは当てにしない。 */
        exec_pending_finish();
        ring3_context_restore(&caller_context);
        _enable();
        if (g_longjmp_reason == EXEC_LJ_PARK) return g_longjmp_id;
        return 0;
    }

#ifdef OS32_R1_FIXTURE
    r1_fixture_resume((int)app_id);
#endif
    appslot_resume_commit((int)app_id);
    appslot_mark_scheduled((int)app_id, tick_count);   /* 票 T9 §12 S6 */
    /* A parked AS may have been poisoned by a foreign lease revoke. Consume
     * its abort before loading its PD or returning the saved frame to USER. */
    g_cur_app = a;
    ring3_abort_check();
    exec_restore_context((int)app_id);
    /* cli → TSS.ESP0 → CR3 → popad; iretd を割り込み禁止で一続きに。
     * iretd が保存済み EFLAGS (IF=1) を復元するのでアプリ側の IF は変わらない。 */
    ring3_resume(a->frame, a->as->pd_phys, &kernel_tss);
    return 0;   /* 到達しない */
}

/* ======================================================================== */
/*  exec_kill — 止めてあるアプリを起こさずに畳む (KAPI v44、決裁 D9-6)       */
/*                                                                          */
/*  CTRL+STOP は「いま走っているアプリ」宛にしか立たない (IRQ1 の時点で       */
/*  カーネルが知っているのはそれだけ) ので、止めてあるアプリを畳む口が別に    */
/*  要る。これが無いと resume されないまま固まったアプリを永久に畳めない。    */
/*  owner 1 (WM top-level) からのみ。走っている本人には OS32_ERR_STALE。      */
/*                                                                          */
/*  票 T9 D8 以後、畳むのは **id とその子孫** (起動要求表の child を末尾まで   */
/*  辿ったもの) で、順番は **末尾から**。1 本分の手順が exec_kill_one。        */
/* ======================================================================== */
static void exec_kill_one(int id)
{
    AppSlot *a = appslot_get(id);
    if (!a) return;
    /* 走っていないので CR3 は master のまま。owner も 1 のまま動かさない
     * — 回収は全部 ID を明示して呼ぶ (D3)。 */
    exec_reclaim_resources(id);
    exec_teardown_app(a);
    appslot_reclaim(id);
    exec_notify_owned(id, EXEC_KIND_ABORTED);
    /* 生存アプリの集合が変わる瞬間 = transition。G7 の switch ではない。 */
    ring3_transition_count++;
}

i32 exec_kill(i32 app_id)
{
    if (font_guard_active) return OS32_ERR_BUSY;
    int chain[APP_MAX_APPS];
    int n;
    int i;
    int rc = appslot_kill_check((int)app_id);
    if (rc < 0) return rc;

    /* 票 T9 D8: 「id とその子孫を末尾から回収」に固定する。端末 → sh → 子の
     * ように要求表が連鎖しているとき、途中の 1 本だけを畳むと残りが孤児に
     * なる (WM の forget と CANCEL の DONE も壊れる — 票 §9 blocker 2)。
     * 末尾から畳むのは、各段の回収通知が親の表を DONE + child = 0 に
     * するため — 先に親を畳むと、まだ生きている子が誰の表にも載らなくなる。*/
    n = launch_chain((int)app_id, chain, APP_MAX_APPS);
    if (n <= 0) { exec_kill_one((int)app_id); return 0; }
    for (i = n - 1; i >= 0; i--) {
        /* 末尾側が既に畳まれている / 走っている本人だった場合は飛ばす
         * (先頭 app_id は上で検査済み)。 */
        if (chain[i] != (int)app_id && appslot_kill_check(chain[i]) < 0) continue;
        exec_kill_one(chain[i]);
    }
    return 0;
}

/* ======================================================================== */
/*  exec_app_state — 0=空き / 1=走っている / 2=park 中 (KAPI v44、任意)      */
/* ======================================================================== */
i32 exec_app_state(i32 app_id)
{
    return appslot_state((int)app_id);
}

/* ======================================================================== */
/*  exec_abort_clear — CTRL+STOP の要求を降ろす (KAPI v45、決裁 A1)          */
/*                                                                          */
/*  IRQ1 は宛先を選べず「いま走っているアプリ」に立てるが、契約 T6 の宛先は  */
/*  フォーカス窓のアプリ。WM (owner 1) がフォーカス窓の owner を見て、       */
/*  走っている本人でなければこれで本人の要求を降ろし、フォーカス窓の ID を   */
/*  exec_kill で畳む。降ろさなければ本人が次の syscall で畳まれてしまう      */
/*  (= 意図しない 1 本が死ぬ)。要求以外の状態は何も動かさない。              */
/*  0 = 降ろした / 要求が無かった、OS32_ERR_INVAL = owner 1 以外。           */
/* ======================================================================== */
i32 exec_abort_clear(void)
{
    if (font_guard_active) return OS32_ERR_BUSY;
    return appslot_abort_clear();
}
