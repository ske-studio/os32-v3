/* ========================================================================
 *  sh_redraw.inc — SHELL_AS_APP の行再描画 (票 T9 実装レビュー blocker 1)
 *
 *  ui.c が #ifdef SHELL_AS_APP の中で #include する。拡張子が .c でないのは
 *  build/programs.mk の SHELL_SRC が userland/shell 下の .c を wildcard で
 *  拾い、独立した .c にすると常駐 shell.bin のリンクにも空の .o が混ざる
 *  ため (受入 S7 は常駐のバイト一致)。
 *
 *  なぜ座標を使わないか: kernel/console.c は con_sink 有効中 (= GUI 中) に
 *  cursor_x / cursor_y を進めない (K6C-2) ので console_get_cursor_* は常駐
 *  コンソールの古い座標を返す。console_set_cursor が積む CURSOR レコードも
 *  端末側は 40 桁モデルなので x >= 40 や未到達行では**黙って捨てる**。常駐の
 *  redraw_line をそのまま使うと、補完後のカーソル復元ループが同じ座標の
 *  CURSOR を繰り返し送るだけで入力位置が戻らず、別の行を上書きする。
 *
 *  代わりに PRINT のバイト列だけで組み立てる (端末の T4 セルモデルの解釈):
 *    - いま描いてある行の**純粋な延長** (前方一致 + カーソルが末尾) なら
 *      増えたバイトだけ足す。改行もしない — TAB 補完の最頻ケース。
 *    - それ以外は '\n' で次の行へ下り、プロンプトから行全体を描き直す。
 *      ('\r' は行頭へ戻るだけで消さないので、短くなった行が残ってしまう)
 *    - カーソルが行末より前なら BS (0x08) で戻す。端末の BS は 1 セル左へ
 *      動くだけで消さない。全角は 2 桁なので 3 バイト列では 2 回送る。
 *
 *  ホスト TDD (tools/tests/sh_shell_host.c) はこのファイルをそのまま
 *  #include して「延長は差分印字 / 非延長は行の作り直し」を見る。
 * ======================================================================== */

/* いま画面に出ている入力行の写し。sh_drawn_len < 0 は「行が壊れている
 * (候補一覧を挟んだ、カーソルを動かした等) ので必ず作り直す」の印。
 * sh_drawn_pos は**画面カーソルがどのバイト位置にあるか**。差分印字は
 * 画面カーソルが行末にあるときしか成り立たないので、内容の前方一致だけで
 * なくこれも見る (CUI 直起動で `hel` → LEFT → TAB としたとき、写しは
 * `hel` のままでも画面カーソルは 1 つ左にあり、差分だけ出すと `hep ` に
 * なってしまう)。 */
static char sh_drawn[CMD_BUF_SIZE];
static int  sh_drawn_len = 0;
static int  sh_drawn_pos = 0;

static void sh_set_drawn(const char *buf, int len, int pos)
{
    int i;
    if (len < 0 || !buf) { sh_drawn_len = -1; sh_drawn_pos = -1; return; }
    if (len > CMD_BUF_SIZE - 1) len = CMD_BUF_SIZE - 1;
    for (i = 0; i < len; i++) sh_drawn[i] = buf[i];
    sh_drawn_len = len;
    sh_drawn_pos = (pos < 0 || pos > len) ? len : pos;
}

/* buf[from, to) をそのまま印字する (shell_print_utf8 は NUL 終端を取る) */
static void sh_put_run(const char *buf, int from, int to)
{
    char tmp[CMD_BUF_SIZE];
    int i, n = 0;
    for (i = from; i < to && n < CMD_BUF_SIZE - 1; i++) tmp[n++] = buf[i];
    tmp[n] = '\0';
    if (n > 0) g_api->shell_print_utf8(tmp, ATTR_WHITE);
}

/* buf[cursor, len) の**桁数**だけ BS を送ってカーソルを左へ戻す */
static void sh_cursor_back(const char *buf, int cursor, int len)
{
    int i;
    for (i = cursor; i < len; i++) {
        u8 c = (u8)buf[i];
        if ((c & 0xC0) == 0x80) continue;          /* UTF-8 の後続バイト */
        g_api->shell_putchar(0x08, ATTR_WHITE);
        /* 3 バイト列 (CJK) は 2 桁ぶん進んでいる */
        if ((c & 0xF0) == 0xE0) g_api->shell_putchar(0x08, ATTR_WHITE);
    }
}

/* sh_backspace_tail — 行末の 1 バイトを消す (SHELL_AS_APP の BS)
 *
 *  new_len は**消した後**の長さで、buf[new_len] がこれから消えるバイト。
 *  端末の BS (0x08) はカーソルを 1 セル左へ動かすだけでセルを消さない
 *  (userland/libos32term/src/model.rs の write_inner) ので、BS だけを出すと
 *  画面に文字が残ったままバッファだけ縮む (`echo abc` -> BS BS -> `x` で
 *  画面 `echo axc` / バッファ `echo ax`)。BS + 空白 + BS で上書きして消す。
 *  3 バイト列の先頭は 2 セルぶん。CUI の console は BS で消すので、この
 *  経路は SHELL_AS_APP だけ (常駐の BS は 1 バイトも変えていない)。
 *
 *  消した後はカーソルが新しい行末に来るので、写しもそこで確定する。 */
/* sh_erase_cells — 消したバイト 1 つぶんを画面から消す (B5 で公開)
 * 3 バイト列の先頭は 2 セル。cmd_script.c の `ask` も同じものを使う。 */
void sh_erase_cells(char removed)
{
    int cells = ((((u8)removed) & 0xF0) == 0xE0) ? 2 : 1;
    int i;

    for (i = 0; i < cells; i++) g_api->shell_putchar(0x08, ATTR_WHITE);
    for (i = 0; i < cells; i++) g_api->shell_putchar(' ', ATTR_WHITE);
    for (i = 0; i < cells; i++) g_api->shell_putchar(0x08, ATTR_WHITE);
}

static void sh_backspace_tail(char *buf, int new_len)
{
    sh_erase_cells(buf[new_len]);
    buf[new_len] = '\0';
    sh_set_drawn(buf, new_len, new_len);
}

static void redraw_line(const char *buf, int len, int cursor)
{
    int i;
    /* 差分だけ足せるのは「画面カーソルが行末にあり、いまの行がその純粋な
     * 延長で、新しいカーソルも行末」のときだけ。 */
    int extend = (sh_drawn_len >= 0 && sh_drawn_pos == sh_drawn_len &&
                  len >= sh_drawn_len && cursor == len);

    if (extend) {
        for (i = 0; i < sh_drawn_len; i++) {
            if (buf[i] != sh_drawn[i]) { extend = 0; break; }
        }
    }

    if (extend) {
        sh_put_run(buf, sh_drawn_len, len);
    } else {
        g_api->shell_putchar('\n', ATTR_WHITE);
        show_prompt();
        sh_put_run(buf, 0, len);
        sh_cursor_back(buf, cursor, len);
    }

    sh_set_drawn(buf, len, cursor);
    prev_draw_len = len;
}
