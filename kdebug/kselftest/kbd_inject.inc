/* ------------------------------------------------------------------------ */
/*  打鍵の注入リングと「印の無い resume は拒否」(票 K7 の受入 I5)            */
/*                                                                          */
/*  GUI 中の kbd_getchar は第 2 の park 点になった。壊れたときに実機で見える */
/*  のは「端末に打っても文字が出ない」か「GUI ごと固まる」だけで、原因が     */
/*  遠い。ブート時に踏むのは 2 つ:                                           */
/*    (1) 256B の環 — 積んだ順に 1 バイトずつ出る (UTF-8 の並びを変えない)、 */
/*        あふれは新しい方を捨てる、破棄で空、読み手未確立の注入は拒否。     */
/*    (2) 印の無いフレームは起こせない (C6 の規則が PARKED / WAIT_KEY /      */
/*        WAIT_POLL の 3 つに効く)。resume の切替点が緩むとフレームが宙に    */
/*        浮く。D8 の tick の間引きが表の検査より先に効くことも見る。        */
/* ------------------------------------------------------------------------ */
static void test_kbd_inject(void)
{
    u32 bad = kst_kbd_inject();
    check((bad & (1u << 0)) == 0, "kbd_inject boot has no fullscreen owner and refuses without reader");
    check((bad & (1u << 1)) == 0, "kbd_inject keeps UTF-8 byte order (FIFO)");
    check((bad & (1u << 2)) == 0, "kbd_inject take on empty ring returns 0");
    check((bad & (1u << 3)) == 0, "kbd_inject overflow drops the newest byte");
    check((bad & (1u << 4)) == 0, "kbd_inject discard empties the ring");
}
