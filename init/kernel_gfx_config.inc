static void kernel_gfx_config(void)
{
    /* グラフィクスバックエンドの強制指定 (票 H2b、契約 G5)。
     * /etc/system.cfg の GFX= を **⑥ (gfx_boot_reserve) より前** に読んで HAL
     * へ渡す — 予約・写像・BB の候補は GFX= と識別で決まる (TASK_T1_LEDGER
     * §3-3 ⑥、T1e で exec_init の前へ前倒し。依存は vfs_read だけで、ルートは
     * マウント済み)。
     *   GFX=pc98   9801 プレーン強制 (NP21/W でプレーン経路を回帰試験する)
     *   GFX=pegc   PEGC 強制 (probe が通らなければ 9801)
     *   GFX=cirrus Cirrus GD54xx 強制 (probe が通らなければ 9801、票 H3)
     *   未指定     auto = probe 順 (Cirrus → PEGC → 9801)
     * K4 の GUI= と同じ流儀: ここで読み、サブシステム側の変数へ渡す。 */
    char gfxmode[16];
    char gfmode[16] = {0};
    (void)sysconfig_get_str(SYS_SYSTEM_CFG, "GFX", gfmode, sizeof(gfmode));
    gf_production_configure(gfmode, sysconfig_get_int(SYS_SYSTEM_CFG, "GUI", 0));
    if (sysconfig_get_str(SYS_SYSTEM_CFG, "GFXDIAG", gfxmode, sizeof(gfxmode)) > 0) {
        gfx_diag_stripes = kstrcmp(gfxmode, "stripes") == 0;
        gfx_diag_altwin = kstrcmp(gfxmode, "altwin") == 0;
    }
    if (sysconfig_get_str(SYS_SYSTEM_CFG, "GFX", gfxmode, (int)sizeof(gfxmode)) > 0) {
        if (kstrcmp(gfxmode, "pc98") == 0) {
            gfx_set_backend_pref(GFX_PREF_PC98);
        } else if (kstrcmp(gfxmode, "pegc") == 0) {
            gfx_set_backend_pref(GFX_PREF_PEGC);
        } else if (kstrcmp(gfxmode, "geforce") == 0) {
            gfx_set_backend_pref(gfx_geforce_pref(sysconfig_get_int(SYS_SYSTEM_CFG, "GUI", 0)));
        } else if (kstrcmp(gfxmode, "cirrus") == 0) {
            gfx_set_backend_pref(GFX_PREF_CIRRUS);
        } else {
            gfx_set_backend_pref(GFX_PREF_AUTO);   /* auto / 未知の値 */
        }
    }
}
