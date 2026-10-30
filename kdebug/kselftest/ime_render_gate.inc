/* ------------------------------------------------------------------------ */
/*  KAPI ime_set_render は常駐側だけ (2026-09-26、代行レビュー P2。           */
/*  docs/design/kernel/gui.md#落とし穴)。カーネルは控えた IME_Render 表の関数を以後 CPL=0 で  */
/*  呼ぶので、アプリ由来 (owner != 1 か ring3_call_from_user 真) の登録は     */
/*  kernel/gui.c の gui_ime_set_render が黙って断る。ここではディスパッチ中の */
/*  アプリを装って NULL を渡し (門が壊れていても描画先は TVRAM 版のまま =     */
/*  起動時の値なので害が無い)、断った回数が増えることを見る。常駐側の直呼び  */
/*  (owner 1、ディスパッチの外) の NULL は通って回数が増えない。表の全体は    */
/*  ホスト (tools/tests/ring3_guard_host.c の 5) が持つ。                      */
/* ------------------------------------------------------------------------ */
static void test_ime_render_gate(void)
{
    int saved = ring3_in_syscall;
    int saved_depth = ring3_wm_depth;
    int saved_owner = res_owner_get();
    u32 base = gui_ime_render_rejected;

    kdi_set_ring3_wm_depth(0);
    kdi_set_ring3_in_syscall(1);               /* アプリ (ID 2) の syscall を装う */
    res_owner_set(2);
    gui_ime_set_render((void *)0);
    check(gui_ime_render_rejected == base + 1u,
          "ime-render: app-origin registration refused");

    kdi_set_ring3_in_syscall(0);               /* 常駐側 (owner 1) の直呼び */
    res_owner_set(1);
    gui_ime_set_render((void *)0);
    check(gui_ime_render_rejected == base + 1u,
          "ime-render: resident shell registration passes");

    res_owner_set(saved_owner);
    kdi_set_ring3_wm_depth(saved_depth);
    kdi_set_ring3_in_syscall(saved);
}
