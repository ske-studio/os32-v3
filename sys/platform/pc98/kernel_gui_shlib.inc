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
        gshell_warning = "GUI shlib: rebuild required (KAPI data layout) -> CUI shell";
        if (!serial_gate_active())
            serial_puts_polled("[boot] " SYS_SHLIB_GUI ": rebuild required (KAPI data layout)"
                               " -> CUI shell. rebuild /sys and deploy\n");
    } else if (shlib_reject_reason() == SHLIB_REJECT_MIN_API) {
        gshell_warning = "GUI shlib: needs a newer kernel -> CUI shell";
        gshell_warning_help = "  update the kernel first (hsync boot / deploy-kernel)";
        if (!serial_gate_active()) {
            serial_puts_polled("[boot] " SYS_SHLIB_GUI ": needs KAPI v");
            serial_diag_u32(shlib_reject_min_api());
            serial_puts_polled(" > kernel v");
            serial_diag_u32(KAPI_VERSION);
            serial_puts_polled(" -> CUI shell.");
            serial_puts_polled(gshell_warning_help);
            serial_puts_polled("\n");
        }
    } else {
        return 1;
    }
    console_text_gdc_start();
    tvram_clear();
    return 0;
}
