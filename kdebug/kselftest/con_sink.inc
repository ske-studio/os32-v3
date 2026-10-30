/* ------------------------------------------------------------------------ */
/*  console シンクのリング (票 K6C の受入 C2): GUI モード中のカーネル出力を  */
/*  溜める 8KB の環。レコード境界で切ること・あふれで **古い方**を捨てる     */
/*  こと・CUI 復帰で捨てること・読み手が 1 本であることが崩れると、端末      */
/*  アプリには「出力が出ない」か「途中で化ける」としか見えず原因が遠い。      */
/*  ホスト試験 (tools/tests/test_con_sink.py) と同じ形をブート時にも踏む。    */
/* ------------------------------------------------------------------------ */
static void test_con_sink(void)
{
    u32 bad = kst_con_sink();
    check((bad & (1u << 0)) == 0, "con_sink push/read (record round-trip)");
    check((bad & (1u << 1)) == 0, "con_sink CLEAR / CURSOR records");
    check((bad & (1u << 2)) == 0, "con_sink read cuts on a record boundary");
    check((bad & (1u << 3)) == 0, "con_sink overflow drops the oldest record");
    check((bad & (1u << 4)) == 0, "con_sink discards on return to CUI");
    check((bad & (1u << 5)) == 0, "con_sink single reader (owner reclaim)");
    check((bad & (1u << 6)) == 0, "con_sink EXIT record (child exit, T7 E1)");
}
