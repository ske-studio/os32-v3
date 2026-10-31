/* Fault and abort termination entry points. Included by exec.c. */
/* ======================================================================== */
/*  ring3_fault_kill — CPL=3 由来の #PF/#GP でアプリを kill (v2 M1e)        */
/*                                                                          */
/*  #PF/#GP ハンドラ (arch/x86/kernel/isr_handlers.c) がフォールトフレームの         */
/*  CS.RPL=3 (= CPL=3 由来) を検出したときに呼ぶ。カーネルを巻き込まず       */
/*  **その ID だけ**を畳んでシェル (WM) に戻す (D4)。                        */
/*  資源を触らず longjmp。両着地点の exec_pending_finish が通常文脈で回収。 */
/*  この関数は longjmp するので戻らない。                                    */
/* ======================================================================== */
/* h3 の syscall_abort 観測点として ring3_abort_check 内の呼出しを残す。 */
static void ring3_kill_kind(int kind) __attribute__((noinline));
static void ring3_kill_kind(int kind)
{
    if (font_guard_active) font_guard_fault(0, 0, 0);
    fault_kill_count++;
    if (kind == EXEC_KIND_FAULT && ring3_wm_depth > 0) {
        ring3_wm_fault_count++;     /* 深さを 0 に戻す前に数える */
    }
    /* syscall 入口などの通常の安全点は回収へ直行。IRQ/例外だけ移譲。 */
    if (!kctx_irq_depth && !kctx_exc_depth)
        exec_exit(EXEC_ERR_FAULT, kind);
    else
        exec_pending_transfer(kind);
}

void ring3_fault_kill(void)
{
    ring3_kill_kind(EXEC_KIND_FAULT);   /* 戻らない */
}

/* CTRL+STOP で畳む口 (票 §1 事実 15)。後始末は fault とまったく同じで、
 * 違うのは記録する種別だけ — ランナーが「時間切れで畳んだ」と「落ちた」を
 * 分けられるようにする。 */
void ring3_abort_kill(void)
{
    ring3_kill_kind(EXEC_KIND_ABORTED);   /* 戻らない */
}
