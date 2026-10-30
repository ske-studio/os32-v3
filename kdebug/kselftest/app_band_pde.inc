/* ------------------------------------------------------------------------ */
/*  T2c の高位アプリ帯 (票 docs/tasks/v3/TASK_T2_APPBAND.md): 未使用 PDE は  */
/*  空のまま、写像時に必要なアプリ固有 PT を疎確保する。USER が              */
/*  master の PDE/PT へ漏れないこと。ここが壊れると CPL=3 アプリが master の  */
/*  ページテーブルを書き換えられる (= 任意物理への読み書き) が、動いている    */
/*  ように見えてしまうので毎回ブート時に見る。                                */
/* ------------------------------------------------------------------------ */
static void test_app_band_pde(void)
{
    int rc = paging_app_band_selftest();
    check(rc == 0, "app band PDEs (private PTs, USER never reaches master)");
    if (rc != 0) {
        kprintf(0xC1, "[selftest]   paging_app_band_selftest rc=%d\n", rc);
    }
}
