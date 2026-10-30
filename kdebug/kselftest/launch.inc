/* ------------------------------------------------------------------------ */
/*  起動要求表 (票 T9 D3 の受入): GUI 中の外部プログラム起動は端末 / sh から */
/*  カーネルの表を通って WM へ渡る。ここが壊れたとき実機で見えるのは         */
/*  「sh> から何も起動しない」「プロンプトに戻らない」「2 回目以降が         */
/*  ERR_FULL」だけで原因が遠いので、遷移の骨だけをブート時に踏む。          */
/*  ホスト試験 (tools/tests/test_launch.py) と同じ形。                       */
/* ------------------------------------------------------------------------ */
static void test_launch(void)
{
    u32 bad = launch_selftest();
    check((bad & (1u << 0)) == 0, "launch: child exit marks DONE and clears child");
    check((bad & (1u << 1)) == 0, "launch: a finished row is handed over once");
    check((bad & (1u << 2)) == 0, "launch: requester exit becomes an orphan KILL");
}
