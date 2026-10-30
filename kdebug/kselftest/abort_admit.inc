/* ------------------------------------------------------------------------ */
/*  GUI 中の CTRL+STOP は WM が宛先を決める (票 T9 §12 S6)                   */
/*                                                                          */
/*  IRQ1 は「そのとき走っていた slot」しか知らないが、GUI 配下の宛先は       */
/*  フォーカス窓の連鎖の末尾 (D8) で、それを解決できるのは WM だけ。         */
/*  ここが崩れると実機では「CTRL+STOP で端末まで消える」(立て過ぎ) か        */
/*  「暴走したアプリを畳めない」(立て無さ過ぎ) としか見えない。              */
/* ------------------------------------------------------------------------ */
static void test_abort_admit(void)
{
    u32 bad = appslot_abort_admit_selftest();
    check((bad & (1u << 0)) == 0, "CUI keeps the CTRL+STOP escape hatch (K2)");
    check((bad & (1u << 1)) == 0, "GUI running app: target is left to the WM");
    check((bad & (1u << 2)) == 0, "GUI still kills a runaway app (2s no syscall)");
    check((bad & (1u << 3)) == 0, "CTRL+STOP never lands on the shell band");
    check((bad & (1u << 4)) == 0, "GUI keeps the K5c path (inside gui_call OP_WAIT)");
}
