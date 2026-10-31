/* 共有ライブラリを断っていたら GUI は動かない。CUI に落として
 * 案内する。**直し方は理由で逆向き**なので案内を分ける:
 *   配置違い … GUI アプリが共有メモリの番地を取り違える。/sys を
 *              作り直して配備 (票 TASK_KAPI_DATA_FIELDS)
 *   要求版   … ライブラリの方が新しい。カーネルを先に更新する
 *              (実装レビュー ラリー 2 の B1。以前は案内が無く、
 *              gshell の読み込み失敗としか出なかった) */
static int kernel_gui_shlib_ready(void)
{
    if (shlib_reject_reason() == SHLIB_REJECT_LAYOUT) {
        kprintf(ATTR_YELLOW, "[boot] %s: rebuild required (KAPI data layout)"
                " -> CUI shell. rebuild /sys and deploy\n", SYS_SHLIB_GUI);
        console_text_gdc_start();
        tvram_clear();
        tvram_print(0, 0, "GUI shlib: rebuild required (KAPI data layout) -> CUI shell",
                    TATTR_RED);
        return 0;
    } else if (shlib_reject_reason() == SHLIB_REJECT_MIN_API) {
        kprintf(ATTR_YELLOW, "[boot] %s: needs KAPI v%u > kernel v%u -> CUI shell."
                " update the kernel first\n", SYS_SHLIB_GUI,
                shlib_reject_min_api(), (u32)KAPI_VERSION);
        console_text_gdc_start();
        tvram_clear();
        tvram_print(0, 0, "GUI shlib: needs a newer kernel -> CUI shell",
                    TATTR_RED);
        tvram_print(0, 1, "  update the kernel first (hsync boot / deploy-kernel)",
                    TATTR_RED);
        return 0;
    }
    return 1;
}
