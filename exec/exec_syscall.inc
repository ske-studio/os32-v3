/* Ring3 syscall dispatch and completed-syscall STOP parking. Included by exec.c. */
/* ======================================================================== */
/*  ring3_syscall_dispatch — CPL=3 からの int 0x80 ディスパッチャ (v2 M2d)   */
/*                                                                          */
/*  arch/x86/kernel/ring3_entry.asm の int80_stub が pushad 後のフレーム先頭を渡す。  */
/*  フレーム (u32 配列, pushad + CPU が積んだ例外フレーム):                   */
/*    [0..7]=pushad (EDI,ESI,EBP,ESP,EBX,EDX,ECX,EAX)  → EAX=[7]             */
/*    [8]=EIP [9]=CS [10]=EFLAGS [11]=userESP [12]=userSS                    */
/*                                                                          */
/*  eax(=[7]) がスタブの積んだ slot。範囲外は即 kill (CONTRACTS C4)。        */
/*  本物の KAPI 表 (KAPI_ADDR: [magic][version][fn0..]) から wrap を引き、    */
/*  ユーザスタック (userESP+4, スタブの ret アドレス分を飛ばす) から引数を    */
/*  コピーして呼ぶ。戻り値は eax スロット([7])へ書く → popad で復元される。   */
/*  現 CR3 はアプリ PD のまま呼ぶ (ユーザポインタ引数がアプリ帯で解決される)。*/
/*  sys_exit は wrap → kapi_sys_exit が teardown+longjmp するのでここへ戻らない。*/
/*                                                                          */
/*  K5b: フレーム先頭を g_cur_frame に控える。gshell の op_wait が            */
/*  exec_park() を呼んだとき、この 13 語をそのまま AppSlot へ写して           */
/*  「CPL=3 の続き」を保存する (D2 の (b): カーネルスタックは 1 本のまま)。   */
/* ======================================================================== */

/* kprintf の可変引数上限。固定引数 syscall のコピー長とは独立。 */
#define RING3_ARG_WINDOW  64u
STATIC_ASSERT(KAPI_PRINT_ARG_LIMIT == (RING3_ARG_WINDOW - KAPI_PRINT_FIXED_WORDS * sizeof(u32)) / sizeof(u32), print_arg_window);

/* Each park origin checks admission before saving its selected frame here. */
static void exec_park_transfer(int id, AppSlot *a, const u32 *frame, void (*commit)(void))
{
    u32 k;
    for (k = 0; k < APP_FRAME_WORDS; k++) a->frame[k] = frame[k];
    exec_heap_save_state(&a->exec_heap_used);
    ring3_context_clear();     /* This syscall ends without passing the WM exit. */
    paging_load_cr3(paging_kernel_pd_phys());
    commit();
    exec_restore_context(APP_ID_SHELL);
    g_longjmp_reason = EXEC_LJ_PARK;
    g_longjmp_id = id;
    exec_longjmp(a->jmpbuf);
}

/* Fifth park origin: only after the entire KAPI/callback has returned.
 * No cleanup here; the existing WM top-level selects the owner to kill. */
static void exec_park_stop(u32 *frame)
{
    if (font_guard_active) return;
    int id = appslot_cur();
    AppSlot *a = appslot_get(id);
    if (!a || !appslot_stop_pending() || !a->gui || !a->cpl3 ||
        a != g_cur_app || a->state != APP_STATE_RUNNING || a->in_op_wait ||
        ring3_wm_depth || kctx_irq_depth || kctx_exc_depth) return;
    exec_park_transfer(id, a, frame, appslot_park_stop_commit);
}

void __cdecl ring3_syscall_dispatch(u32 *frame)
{
    /* VM frames also carry ES/DS/FS/GS after SS. This is a guest INT,
     * never an OS caller, even when EAX happens to name a valid KAPI slot. */
    if (frame[V86I_EFLAGS] & EFLAGS_VM) {
        v86_int80(frame);
        return;
    }
    u32 slot     = frame[APP_FRAME_EAX];         /* スタブが積んだ slot (eax) */
    u32 user_esp = frame[APP_FRAME_ESP];        /* CPL=3 の ESP (int が積んだ) */
    const void *args_src;
    u32 nbytes;
    u32 wrapptr;
    u32 *prev_frame = g_cur_frame;
    int prev_in_syscall = ring3_in_syscall;
    CallerAccessFrame prev_caller;

    /* Capture before callbacks; nonlocal exits clear at transfer and landing. */
    if (!caller_access_enter(&prev_caller, CALLER_USER)) {
        ring3_caller_reject_count++;
        ring3_fault_kill();
        return;
    }
    g_cur_frame = frame;
    /* WM の文脈の深さは **CPL=3 の syscall の入口で必ず 0** (不変条件)。
     * WM を longjmp で抜けた (park / kill) 後に 1 が残っていても、ここで
     * 立ち直る — 残ると 3 つの門がアプリのポインタを素通しする穴になる。 */
    ring3_wm_depth = 0;
    ring3_wm_shell_gen = 0;   /* T7b-b2: 深さと同じく WM の印も 0 から始まる */
    /* 暴走判定の起点 (票 T9 §12 S6b)。**代入 1 つだけ** — ここは hot path。
     * start / resume だけを起点にしていると、GetMessage 型の GUI アプリ
     * (端末) が WM の op_wait の中で待っている間は更新されず、2 秒待った
     * だけで「暴走」に見えた。KAPI を呼んでいる限りここを通る。 */
    if (g_cur_app) g_cur_app->last_kernel_tick = tick_count;

    /* --- CTRL+STOP の要求があればここで畳む (契約 T6) --- */
    ring3_abort_check();        /* 要求があれば longjmp して戻らない */

    /* --- GUI 入力ポンプ (票 K2-1、契約 T6 / T8 の X4) ---
     * フォールトガード (ring3_in_syscall) を立てる **前** に回す。 */
    ring3_gui_pump();

    /* Private value-copy entry used by the CPL3 sys_ls shim. No USER callback
     * address crosses this boundary; raw public slot 12 is guarded as well. */
    if (slot == RING3_LS_CALL) {
        ring3_in_syscall = 1;
        frame[APP_FRAME_EAX] = (u32)ring3_ls_dispatch(user_esp);
        goto syscall_complete;
    }

    /* 範囲外 slot はワイルド呼び出し → アプリだけ kill (カーネルを飛ばさない)。 */
    if (slot >= (u32)KAPI_FUNC_COUNT) {
        ring3_fault_kill();
    }

    /* 本物の表から wrap_<slot> を取得 ([magic][version] の後が fn 表)。 */
    wrapptr = ((const u32 *)KAPI_ADDR)[KAPI_FN_TABLE_FIRST + slot];
    args_src = (const void *)(user_esp + sizeof(u32));
    nbytes = (u32)kapi_argsize[slot];

    /* --- (核) フォールトガードを立てる (v2 M2e) --- */
    ring3_in_syscall = 1;

    int vcall = v86_call_dispatch(slot, (void *)wrapptr, args_src, nbytes, &frame[APP_FRAME_EAX]);
    if (vcall < 0) ring3_fault_kill();
    if (vcall) goto syscall_complete;

    /* Variadic input is copied by B1 on demand. Never read/copy an unused
     * word past the caller's mapped argument tail, or emit a partial line. */
    if (slot == KAPI_SLOT_KPRINTF) {
        if (!kapi_print_call(args_src)) ring3_fault_kill();
        frame[APP_FRAME_EAX] = 0;
        goto syscall_complete;
    }

    int strings = kapi_strings_call(slot, (void *)wrapptr, args_src,
                                    nbytes, kapi_argptr[slot], &frame[APP_FRAME_EAX]);
    if (strings < 0) ring3_fault_kill();
    if (strings) goto syscall_complete;

    /* --- (補助) 明示ポインタ引数の早期範囲検証 (v2 M2e) --- */
    {
        u16 ptrmask = kapi_argptr[slot];
        if (ptrmask) {
            const u32 *a = (const u32 *)args_src;
            u32 nfixed = nbytes / sizeof(u32);   /* 固定引数の個数 */
            u32 k;
            for (k = 0; k < nfixed && k < sizeof(ptrmask) * BITS_PER_BYTE; k++) {
                if ((ptrmask & (u16)(1u << k)) && !ring3_ptr_ok(a[k])) {
                    ring3_fault_kill();   /* 範囲外ポインタ → kill、戻らない */
                }
            }
        }
    }

    /* 本物の wrap を呼ぶ (現 CR3 = アプリ PD)。戻り値を eax スロットへ。
     * gui_call(OP_WAIT) → exec_park() はここから longjmp して戻らない。 */
    frame[APP_FRAME_EAX] = kapi_invoke((void *)wrapptr, args_src, nbytes);

syscall_complete:
    /* 正常復帰: ガードを下ろす */
    caller_access_leave(&prev_caller);
    ring3_in_syscall = prev_in_syscall;
    g_cur_frame = prev_frame;

    /* --- 出口でも CTRL+STOP を見る (契約 T6、v1.2 G2 で実測した隙間) --- */
    ring3_abort_check();
    exec_park_stop(frame);
}
