/* Preedit and candidate-list rendering and TVRAM preservation. */

/* ======================================================================== */
/*  プリエディット表示 (TVRAM 25行目 = y=24)                                 */
/* ======================================================================== */

#define IME_PREEDIT_ROW  24   /* TVRAM最終行 (0始まり) */

static int draw_utf8(int x, int y, const char *str, u8 col)
{
    const u8 *p = (const u8 *)str;
    int cur_x = x;
    utf8_decode_t dec;

    if (!g_ime.render) {
        return 0;
    }

    while (*p && cur_x < TVRAM_COLS) {
        dec = utf8_decode(p);
        int w = g_ime.render->putw(cur_x, y, dec.codepoint, col);
        if (w <= 0) {
            break;
        }
        cur_x += w;
        p += dec.bytes_used;
    }
    return cur_x - x;
}

static int page_count(const IME_State *s)
{
    int base = s->page * s->per_page;
    int rem  = s->result_count - base;
    return (rem > s->per_page) ? s->per_page : rem;
}

#define CANDLIST_SAVE_ROWS  3

static u16 *g_cand_save_text = NULL;
static u8  *g_cand_save_attr = NULL;
static int g_cand_has_saved = 0;

static void candlist_save(void)
{
    int rows;
    int top;
    int y, x;

    rows = (g_ime.per_page + IME_CAND_COLS - 1) / IME_CAND_COLS;
    top = IME_PREEDIT_ROW - rows;

    if (g_cand_has_saved) return;
    if (g_ime.render != &g_ime_render_tvram) {
        g_cand_has_saved = 1;
        return;
    }
    if (!g_cand_save_text || !g_cand_save_attr) return;

    for (y = 0; y < rows; y++) {
        for (x = 0; x < TVRAM_COLS; x++) {
            tvram_readchar_at(x, top + y,
                              &g_cand_save_text[y * TVRAM_COLS + x],
                              &g_cand_save_attr[y * TVRAM_COLS + x]);
        }
    }
    g_cand_has_saved = 1;
}

static void candlist_restore(void)
{
    int rows;
    int top;
    int y, x;

    if (!g_cand_has_saved) return;
    if (!g_cand_save_text || !g_cand_save_attr) return;

    rows = (g_ime.per_page + IME_CAND_COLS - 1) / IME_CAND_COLS;
    top = IME_PREEDIT_ROW - rows;

    for (y = 0; y < rows; y++) {
        for (x = 0; x < TVRAM_COLS; x++) {
            u32 offset = (u32)(top + y) * TVRAM_BPR + (u32)x * TVRAM_CELL_BYTES;
            *(volatile u16 *)P2V_IO(TVRAM_BASE + offset) = g_cand_save_text[y * TVRAM_COLS + x];
            *(volatile u8 *)P2V_IO(TVRAM_ATTR + offset) = g_cand_save_attr[y * TVRAM_COLS + x];
        }
    }
    g_cand_has_saved = 0;
}

static void candlist_clear(void)
{
    if (g_cand_has_saved) {
        if (g_ime.render == &g_ime_render_tvram) {
            candlist_restore();
        } else {
            int rows = (g_ime.per_page + IME_CAND_COLS - 1) / IME_CAND_COLS;
            int top = IME_PREEDIT_ROW - rows;
            for (int y = top; y < IME_PREEDIT_ROW; y++)
                g_ime.render->clear_row(y, ATTR_WHITE);
            g_cand_has_saved = 0;
        }
    }
}

static void candlist_draw(void)
{
    int rows = (g_ime.per_page + IME_CAND_COLS - 1) / IME_CAND_COLS;
    int top  = IME_PREEDIT_ROW - rows;
    int base = g_ime.page * g_ime.per_page;
    int n    = page_count(&g_ime);
    int i;
    int y, x;
    int max_page;
    char pbuf[16];

    if (!g_ime.render) {
        return;
    }

    /* 描画前に一旦リスト表示領域をクリア */
    for (y = top; y < IME_PREEDIT_ROW; y++) {
        g_ime.render->clear_row(y, ATTR_WHITE);
    }

    for (i = 0; i < n; i++) {
        int gi = base + i;
        u8  col = (gi == g_ime.candidate_idx) ? ATTR_YELLOW : ATTR_WHITE;
        y = top + (i / IME_CAND_COLS);
        x = (i % IME_CAND_COLS) * IME_CAND_COL_WIDTH;

        /* 番号 "1 " */
        g_ime.render->putc(x, y, '1' + i, ATTR_CYAN);
        g_ime.render->putc(x + 1, y, ' ', col);
        /* 候補文字列 */
        draw_utf8(x + IME_CAND_LABEL_COLS, y, g_ime.results[gi].kanji, col);
    }

    /* ページ位置表示 "1/2" などを末尾（右端付近）に表示 */
    max_page = (g_ime.result_count - 1) / g_ime.per_page + 1;
    pbuf[0] = '0' + (g_ime.page + 1);
    pbuf[1] = '/';
    pbuf[2] = '0' + max_page;
    pbuf[3] = '\0';
    draw_utf8(IME_PAGE_COL, IME_PREEDIT_ROW - 1, pbuf, ATTR_CYAN);
}

static void preedit_clear(void)
{
    if (g_ime.render) {
        g_ime.render->clear_row(IME_PREEDIT_ROW, ATTR_WHITE);
    }
}

static void preedit_draw(void)
{
    int x = 0;
    const u8 *p;

    if (!g_ime.render) {
        return;
    }

    preedit_clear();

    /* モード表示 */
    if (g_ime.mode == IME_MODE_HIRAGANA) {
        g_ime.render->putc(x++, IME_PREEDIT_ROW, '[', ATTR_CYAN);
        x += g_ime.render->putw(x, IME_PREEDIT_ROW, 0x3042, ATTR_CYAN); /* あ */
        g_ime.render->putc(x++, IME_PREEDIT_ROW, ']', ATTR_CYAN);
    } else if (g_ime.mode == IME_MODE_KATAKANA) {
        g_ime.render->putc(x++, IME_PREEDIT_ROW, '[', ATTR_CYAN);
        x += g_ime.render->putw(x, IME_PREEDIT_ROW, 0x30A2, ATTR_CYAN); /* ア */
        g_ime.render->putc(x++, IME_PREEDIT_ROW, ']', ATTR_CYAN);
    }

    if (g_ime.converting) {
        /* 変換候補表示 */
        x += g_ime.render->putw(x, IME_PREEDIT_ROW, 0x25BC, ATTR_YELLOW); /* ▼ */

        if (g_ime.candidate_idx < g_ime.result_count) {
            x += draw_utf8(x, IME_PREEDIT_ROW, g_ime.results[g_ime.candidate_idx].kanji, ATTR_WHITE);
        }

        /* 候補番号表示 */
        if (g_ime.result_count > 1 && x < TVRAM_COLS - IME_COUNT_TEXT_COLS) {
            char nbuf[8];
            int ni;
            g_ime.render->putc(x++, IME_PREEDIT_ROW, '(', ATTR_CYAN);
            nbuf[0] = '0' + ((g_ime.candidate_idx + 1) / 10);
            nbuf[1] = '0' + ((g_ime.candidate_idx + 1) % 10);
            nbuf[2] = '/';
            nbuf[3] = '0' + (g_ime.result_count / 10);
            nbuf[4] = '0' + (g_ime.result_count % 10);
            nbuf[5] = ')';
            nbuf[6] = '\0';
            for (ni = 0; nbuf[ni] && x < TVRAM_COLS; ni++) {
                g_ime.render->putc(x++, IME_PREEDIT_ROW, nbuf[ni], ATTR_CYAN);
            }
        }

        /* 候補リストがONなら展開 */
        if (g_ime.state == IME_ST_CANDLIST) {
            candlist_draw();
        }
    } else if (g_ime.kana_len > 0 || g_ime.rk.preedit[0] != '\0') {
        /* かなバッファ + 未確定ローマ字 */
        x += draw_utf8(x, IME_PREEDIT_ROW, g_ime.kana_buf, ATTR_GREEN);
        
        /* 未確定ローマ字 */
        p = (const u8 *)g_ime.rk.preedit;
        while (*p && x < TVRAM_COLS) {
            g_ime.render->putc(x++, IME_PREEDIT_ROW, (char)*p, ATTR_YELLOW);
            p++;
        }
    }
}

