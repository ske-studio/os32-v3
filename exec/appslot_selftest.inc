/* AppSlot の残存自己試験と退役 import の互換口。appslot.c の状態を共有する。 */

/* 退役した私有 import の互換口。状態を作る試験はホストへ移設済み。 */
u32 appslot_resume_mark_selftest(void)
{
    return 0;
}

/* ======================================================================== */
/*  appslot_gfx_owner_selftest — 画面の所有者の遷移 (票 T8 D1 / D1a)         */
/*                                                                          */
/*  ブート時に踏むのは 2 つ:                                                 */
/*    (0) 遷移: 宣言のある CPL=3 アプリの gfx_init で所有者がその ID へ移り、 */
/*        回収 (appslot_gfx_owner_exit) で WM (1) へ戻る。CUI 中 (gui_mode   */
/*        = 0) は 1 のまま動かない。                                         */
/*    (1) 拒否: GUI 中に宣言の無い CPL=3 が呼んだら OS32_ERR_INVAL で、       */
/*        所有者は動かず gfx_init_reject_count だけが 1 増える。              */
/*                                                                          */
/*  空きスロット (APP_ID_MAX) を一時的に借りる。借りた中身・cur・owner・      */
/*  所有者・カウンタは丸ごと保存して戻す。                                   */
/* ======================================================================== */
/* ======================================================================== */
/*  GUI 中の CTRL+STOP は WM が宛先を決める (票 T9 §12 S6)                   */
/*                                                                          */
/*  壊れたときに実機で見えるのは「CTRL+STOP で関係ないアプリ (端末) まで     */
/*  消える」か「暴走したアプリを畳めない」だけで、原因が遠い。借りた         */
/*  スロットと cur / owner は必ず元へ戻す。ビット 0..n が落ちた項目。        */
/* ======================================================================== */
u32 appslot_abort_admit_selftest(void)
{
    u32 bad = 0;
    int id = APP_ID_MAX;
    AppSlot saved;
    int saved_cur = g_cur;
    int saved_owner = res_owner_get();

    saved = g_slot[id];
    slot_zero(&g_slot[id]);
    g_slot[id].state = APP_STATE_RUNNING;
    g_slot[id].cpl3 = 1;
    g_slot[id].last_kernel_tick = 1000;
    g_cur = id;

    /* (0) CUI 中は従来どおり立てる (K2 の逃げ道を 1 バイトも変えない) */
    if (appslot_abort_admit(0, 1000) != 1) bad |= 1u << 0;
    if (appslot_abort_admit(0, 1000 + APP_RUNAWAY_TICKS) != 1) bad |= 1u << 0;

    /* (1) GUI 中にアプリのコードを割り込んだときは立てない
     * — 宛先は WM が launch_child で解決する (D8) */
    if (appslot_abort_admit(1, 1000) != 0) bad |= 1u << 1;
    if (appslot_abort_admit(1, 1000 + APP_RUNAWAY_TICKS - 1) != 0) bad |= 1u << 1;

    /* (4) ただし gui_call(OP_WAIT) の中なら立てる (K5c の経路)。
     * 割り込まれた文脈は CPL=0 (WM) なので、要求は必ず WM のハンドラが見る。 */
    g_slot[id].in_op_wait = 1;
    if (appslot_abort_admit(1, 1000) != 1) bad |= 1u << 4;
    if (appslot_abort_admit(0, 1000) != 1) bad |= 1u << 4;   /* CUI も従来どおり */
    g_slot[id].in_op_wait = 0;
    if (appslot_abort_admit(1, 1000) != 0) bad |= 1u << 4;

    /* (2) 暴走だけは GUI 中でも立てる = 最後に **カーネルへ入って** から
     * APP_RUNAWAY_TICKS 以上 (tick が一周しても差で見る)。op_wait で待って
     * いるアプリは syscall の入口で控えが進むので、ここには掛からない。 */
    if (appslot_abort_admit(1, 1000 + APP_RUNAWAY_TICKS) != 1) bad |= 1u << 2;
    g_slot[id].last_kernel_tick = 0xFFFFFF00UL;
    if (appslot_abort_admit(1, 0xFFFFFF00UL + APP_RUNAWAY_TICKS) != 1) {
        bad |= 1u << 2;
    }
    if (appslot_abort_admit(1, 0xFFFFFF00UL + 1u) != 0) bad |= 1u << 2;

    /* (3) WM (シェル帯) が走っているときは GUI 中も CUI 中も立てない
     * — CUI では appslot_abort_request 側が弾く (従来どおり)。 */
    g_cur = APP_ID_SHELL;
    if (appslot_abort_admit(1, 0) != 0) bad |= 1u << 3;
    if (appslot_abort_request() != 0) bad |= 1u << 3;

    g_slot[id] = saved;
    g_cur = saved_cur;
    res_owner_set(saved_owner);
    return bad;
}

u32 appslot_gfx_owner_selftest(void)
{
    u32 bad = 0;
    int id = APP_ID_MAX;
    AppSlot saved;
    int saved_cur = g_cur;
    int saved_owner = res_owner_get();
    int saved_gfx = g_gfx_owner;
    u32 saved_reject = gfx_init_reject_count;

    saved = g_slot[id];
    slot_zero(&g_slot[id]);
    g_slot[id].state = APP_STATE_RUNNING;
    g_slot[id].cpl3 = 1;
    g_slot[id].gui = 1;
    g_cur = id;
    res_owner_set(id);
    g_gfx_owner = GFX_OWNER_WM;

    /* (0) 遷移: 宣言ありなら取り、回収で WM へ戻る。CUI 中は動かない。 */
    g_slot[id].hdr_flags = OS32X_FLAG_GFX;
    if (appslot_gfx_claim(0) != 0) bad |= 1u << 0;
    if (g_gfx_owner != GFX_OWNER_WM) bad |= 1u << 0;   /* CUI は触らない */
    if (appslot_gfx_claim(1) != 0) bad |= 1u << 0;
    if (g_gfx_owner != id) bad |= 1u << 0;
    appslot_gfx_owner_exit(GFX_OWNER_WM);              /* 他人の回収では戻らない */
    if (g_gfx_owner != id) bad |= 1u << 0;
    appslot_gfx_owner_exit(id);
    if (g_gfx_owner != GFX_OWNER_WM) bad |= 1u << 0;

    /* (1) 拒否: GUI 中の宣言なしは ERR_INVAL。所有者は動かず、数だけ増える。 */
    g_slot[id].hdr_flags = 0;
    if (appslot_gfx_claim(1) != OS32_ERR_INVAL) bad |= 1u << 1;
    if (g_gfx_owner != GFX_OWNER_WM) bad |= 1u << 1;
    if (gfx_init_reject_count != saved_reject + 1) bad |= 1u << 1;
    /* 票 T8-2: 拒否したアプリは syscall 出口で畳まれる (abort_req が立つ)。 */
    if (!g_slot[id].abort_req) bad |= 1u << 1;
    g_slot[id].abort_req = 0;
    if (appslot_gfx_claim(0) != 0) bad |= 1u << 1;     /* CUI 中は通す */
    if (gfx_init_reject_count != saved_reject + 1) bad |= 1u << 1;
    if (g_slot[id].abort_req) bad |= 1u << 1;          /* 素通しは畳まない */

    /* (2) CUI 専用の宣言 (票 T8-2): GUI からの起動だけを断つ。 */
    if (appslot_cui_only_admit(1, OS32X_FLAG_CUI_ONLY) != OS32_ERR_INVAL)
        bad |= 1u << 2;
    if (appslot_cui_only_admit(0, OS32X_FLAG_CUI_ONLY) != 0) bad |= 1u << 2;
    if (appslot_cui_only_admit(1, 0) != 0) bad |= 1u << 2;

    /* 後始末 */
    g_slot[id] = saved;
    g_cur = saved_cur;
    res_owner_set(saved_owner);
    g_gfx_owner = saved_gfx;
    gfx_init_reject_count = saved_reject;
    return bad;
}
