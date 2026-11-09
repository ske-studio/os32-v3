#include "../drivers/video/geforce/geforce_module.h"
/* Ring3 syscall dispatch and completed-syscall STOP parking. Included by exec.c. */
#define RING3_ARG_WINDOW  64u
STATIC_ASSERT(KAPI_PRINT_ARG_LIMIT == (RING3_ARG_WINDOW - KAPI_PRINT_FIXED_WORDS * sizeof(u32)) / sizeof(u32), print_arg_window);

/* Each park origin checks admission before saving its selected frame here. */
static void exec_park_transfer(int id, AppSlot *a, const struct arch_syscall *frame, void (*commit)(void))
{
    arch_user_save(&a->frame, frame);
    x87_save(&a->x87_state);
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
static void exec_park_stop(struct arch_syscall *frame)
{
    if (font_guard_active) return;
    int id = appslot_cur();
    AppSlot *a = appslot_get(id);
    if (!a || !appslot_stop_pending() || !a->gui || !a->cpl3 ||
        a != g_cur_app || a->state != APP_STATE_RUNNING || a->in_op_wait ||
        ring3_wm_depth || kctx_irq_depth || kctx_exc_depth) return;
    exec_park_transfer(id, a, frame, appslot_park_stop_commit);
}

void exec_syscall_dispatch(struct arch_syscall *view)
{
    struct arch_syscall call = *view;
    u32 result;
    u32 slot = arch_syscall_slot(&call);
    u32 wrapptr;
    struct kapi_arg_source src;
    struct kapi_values values;
    struct arch_syscall *prev_frame = g_cur_frame;
    int prev_in_syscall = ring3_in_syscall;
    CallerAccessFrame prev_caller;

    /* Capture before callbacks; nonlocal exits clear at transfer and landing. */
    if (!caller_access_enter(&prev_caller, CALLER_USER)) {
        ring3_caller_reject_count++;
        ring3_fault_kill();
        return;
    }
    g_cur_frame = &call;
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
    exec_cui_display_abort();
    ring3_abort_check();        /* 要求があれば longjmp して戻らない */

    /* --- GUI 入力ポンプ (票 K2-1、契約 T6 / T8 の X4) ---
     * フォールトガード (ring3_in_syscall) を立てる **前** に回す。 */
    ring3_gui_pump();

    arch_kapi_source(&src, &call);

    /* Private value-copy entry used by the CPL3 sys_ls shim. No USER callback
     * address crosses this boundary; raw public slot 12 is guarded as well. */
    if (slot == RING3_LS_CALL) {
        ring3_in_syscall = 1;
        result = (u32)arch_kapi_legacy_ls(&call);
        goto syscall_complete;
    }

    /* 範囲外 slot はワイルド呼び出し → アプリだけ kill (カーネルを飛ばさない)。 */
    if (slot >= (u32)KAPI_FUNC_COUNT) {
        ring3_fault_kill();
        result = OS32_ERR_INVAL;
        goto syscall_complete;
    }

    /* 本物の表から wrap_<slot> を取得 ([magic][version] の後が fn 表)。 */
    wrapptr = ((const u32 *)KAPI_ADDR)[KAPI_FN_TABLE_FIRST + slot];
    unsigned count = kapi_fixed_count[slot];

    /* --- (核) フォールトガードを立てる (v2 M2e) --- */
    ring3_in_syscall = 1;

    int handled = arch_kapi_legacy_call(&call, slot, (void *)wrapptr, &result);
    if (handled < 0) {
        ring3_fault_kill();
        result = OS32_ERR_INVAL;
        goto syscall_complete;
    }
    if (handled) goto syscall_complete;

    if (!arch_kapi_read_block(&src, 0, count, &values)) {
        ring3_fault_kill();
        result = OS32_ERR_INVAL;
        goto syscall_complete;
    }

    /* --- (補助) 明示ポインタ引数の早期範囲検証 (v2 M2e) --- */
    {
        u16 ptrmask = kapi_argptr[slot];
        if (ptrmask) {
            const u32 *a = values.value;
            u32 nfixed = count;   /* 固定引数の個数 */
            u32 k;
            for (k = 0; k < nfixed && k < sizeof(ptrmask) * BITS_PER_BYTE; k++) {
                if ((ptrmask & (u16)(1u << k)) && !ring3_ptr_ok(a[k])) {
                    ring3_fault_kill();   /* 範囲外ポインタ → kill、戻らない */
                    result = OS32_ERR_INVAL;
                    goto syscall_complete;
                }
            }
        }
    }

    /* 本物の wrap を呼ぶ (現 CR3 = アプリ PD)。戻り値を eax スロットへ。
     * gui_call(OP_WAIT) → exec_park() はここから longjmp して戻らない。 */
    result = arch_kapi_invoke((void *)wrapptr, &values, count);

syscall_complete:
    arch_syscall_result(&call, result);
    /* 正常復帰: ガードを下ろす */
    caller_access_leave(&prev_caller);
    ring3_in_syscall = prev_in_syscall;
    g_cur_frame = prev_frame;

    exec_cui_display_abort();

    /* Resident deadline/pending check only; F5 work runs at a due foreground
     * point after caller restoration and before the resulting abort check. */
    if (geforce_fg_due(tick_count)) geforce_foreground_poll();

    /* --- 出口でも CTRL+STOP を見る (契約 T6、v1.2 G2 で実測した隙間) --- */
    ring3_abort_check();
    exec_park_stop(&call);
}
