/* ------------------------------------------------------------------------ */
/*  GUI モード中の描画抑止 (票 K6C-2)                                        */
/*                                                                          */
/*  シンクが有効なあいだ console.c が従来どおりテキスト VRAM にも描いていた  */
/*  ので、gshell の GFX 画面の上に CUI プログラムの出力が残像として重なって  */
/*  いた (PM 実測 2026-09-12、K7 受入 I2)。実機で見えるのは「左上に古い文字」 */
/*  だけで、シンク側は正常に見えるため原因が遠い。ここで毎回踏む。           */
/*  con_sink_enable/disable を直に使う (console_text_gdc_start はブート画面を */
/*  消してしまう)。 */
/* ------------------------------------------------------------------------ */
static void test_con_sink_render_gate(void)
{
    int sx = console_get_cursor_x();
    int sy = console_get_cursor_y();
    int x  = TVRAM_COLS - 1;
    int y  = TVRAM_ROWS - 1;
    u16 before = 0;
    u16 after = 0;
    u8  attr = 0;

    tvram_readchar_at(x, y, &before, &attr);
    console_set_cursor(x, y);
    con_sink_enable();
    shell_print("Z", TATTR_WHITE);
    tvram_readchar_at(x, y, &after, &attr);
    check(after == before, "console: GUI mode does not draw to text VRAM");
    check(console_get_cursor_x() == x && console_get_cursor_y() == y,
          "console: GUI mode does not advance the logical cursor");
    con_sink_disable();
    console_set_cursor(sx, sy);
}
