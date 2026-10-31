/* CUI ロード失敗時の FDD 代替と致命時の案内。 */
static void kernel_shell_fdd_fallback(int layout_bad)
{
    int rc = exec_run(SYS_SHELL_BIN_FDD);
    if (rc < 0 && rc != EXEC_ERR_FAULT) {
        layout_bad |= exec_layout_rejected();
        if (layout_bad) {
            /* 常駐シェルが KAPI データ欄の配置違い (票
             * TASK_KAPI_DATA_FIELDS)。走らせても malloc が壊れる
             * ので止める。直し方は /sys の作り直しと配備 — NHD は
             * エミュレータ停止中に一式、実機は FD / CD の入れ直し。 */
            kprintf(ATTR_YELLOW, "[boot] FATAL: shell.bin was built for another"
                    " KAPI data layout (kernel %x)\n",
                    (u32)KAPI_DATA_FIELDS_OFF);
            tvram_print(0, 0, "FATAL: shell.bin: rebuild required (KAPI data layout)",
                        TATTR_RED);
            tvram_print(0, 1, "  rebuild /sys (make clean && make all) and deploy it",
                        TATTR_RED);
            tvram_print(0, 2, "  (NHD: whole set while stopped / real HW: boot the new FD)",
                        TATTR_RED);
            for (;;) _halt();
        }
        /* シェルバイナリのロード自体が失敗 — 致命的エラー */
        tvram_print(0, 0, "FATAL: shell.bin load failed", TATTR_RED);
        for (;;) _halt();
    }
}
