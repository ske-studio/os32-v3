/* Real console, TVRAM renderer and candidate restoration on host planes. */
#define main con_sink_fixture_main
#define _start con_sink_fixture_start
#include "con_sink_host.c"
#undef main
#undef _start
#include "../../platform/pc98/ime_render_tvram.c"
#include "../../platform/pc98/ime_display.c"
/* Geometry is a provider seam; exercise common rendering with another size. */
static int host_cols = 80, host_row = 24;
static int test_display_cols(void) { return host_cols; }
static int test_display_row(void) { return host_row; }
#define ime_display_cols test_display_cols
#define ime_display_preedit_row test_display_row
#include "../../kernel/ime/ime.c"
#undef ime_display_cols
#undef ime_display_preedit_row

void *kmemcpy(void *dst, const void *src, u32 n)
{ u8 *d = dst; const u8 *s = src; while (n--) *d++ = *s++; return dst; }
void fep_store_learn(FepStore *dict, const char *yomi, const char *kanji)
{ (void)dict; (void)yomi; (void)kanji; }
int services_public = 1;
volatile u32 services_boot_fault;
int fep_boot_config_get(FepMeta *m, u32 *r) { (void)m; *r = FEP_DICT_RESERVE_KB; return 0; }
int fep_boot_user_path_get(char *p, u32 n) { (void)n; p[0] = 0; return 0; }
int fep_store_open(FepStore *s, const char *p, const char *u)
{ (void)s; (void)p; (void)u; return 0; }
static u16 alloc_text[3 * TVRAM_COLS];
static u8 alloc_attr[3 * TVRAM_COLS];
static u32 allocation_sizes[2];
static int allocations, fail_alloc, frees, close_failure;
static void *freed[2];
void *kmalloc(u32 n)
{
    int i = allocations++;
    check(i < 2, "allocation count");
    allocation_sizes[i] = n;
    if (fail_alloc == i + 1) return NULL;
    return i ? (void *)alloc_attr : (void *)alloc_text;
}
u32 kmalloc_block_size(void *p) { return p == alloc_text ? sizeof(alloc_text) : sizeof(alloc_attr); }
void kfree(void *p) { check(frees < 2, "free count"); freed[frees++] = p; }
int fep_store_close(FepStore *s) { (void)s; return close_failure; }
static void resource_cases(void)
{
    for (int fault = 0; fault < 5; fault++) {
        allocations = frees = 0;
        fail_alloc = fault < 3 ? fault : 0;
        u32 injection = fault == 3 ? SERVICES_FAULT_FEP_TEXT : fault == 4 ? SERVICES_FAULT_FEP_ATTR : 0;
        int rc = ime_display_boot(injection);
        check((rc == 0) == (fault == 0), "resource allocation failure");
        check(allocations == (fault == 1 ? 1 : fault == 3 ? 0 : fault == 4 ? 1 : 2), "allocation order and faults");
        if (allocations) check(allocation_sizes[0] == 480, "text allocation bytes");
        if (allocations == 2) check(allocation_sizes[1] == 240, "attr allocation bytes");
        u32 bytes = (g_cand_save_text ? 480 + BLK_HDR_SIZE : 0) +
                    (g_cand_save_attr ? 240 + BLK_HDR_SIZE : 0);
        check(ime_boot_bytes() == bytes, "resource bytes including headers");
        if (fault == 0) {
            close_failure = 1;
            check(ime_boot_abort() == -1 && !frees && ime_boot_bytes() == bytes,
                  "close failure retains display resources");
            close_failure = 0;
        }
        int count = !!g_cand_save_text + !!g_cand_save_attr;
        void *first = g_cand_save_attr ? (void *)g_cand_save_attr : (void *)g_cand_save_text;
        check(ime_boot_abort() == 0 && frees == count && !ime_boot_bytes(), "abort frees partial resources");
        if (count) check(freed[0] == first, "abort frees attributes before text");
        if (count == 2) check(freed[1] == alloc_text, "abort frees text last");
        check(ime_boot_abort() == 0 && frees == count, "abort idempotent");
    }
    allocations = frees = fail_alloc = 0;
    check(ime_display_boot(0) == 0 && ime_display_boot(SERVICES_FAULT_FEP_TEXT) == 0 && allocations == 2,
          "allocated resources reused");
    ime_display_abort();
}

void __cdecl kprintf(u8 color, const char *fmt, ...)
{ (void)color; (void)fmt; }
static u16 saved_text[3 * TVRAM_COLS];
static u8 saved_attr[3 * TVRAM_COLS];
static u16 before_text[TVRAM_COLS];
static u8 before_attr[TVRAM_COLS];
static u16 window_text[3 * TVRAM_COLS];
static u8 window_attr[3 * TVRAM_COLS];
static void renderer_edges(void)
{
    int y = TVRAM_ROWS - 1;
    int last = y * TVRAM_COLS + TVRAM_COLS - 1;
    g_text_plane[last] = 0x6543;
    g_attr_plane[last] = 0x87;
    check(g_ime_render_tvram.putw(TVRAM_COLS - 1, y, 0x3042, ATTR_GREEN) == 0 &&
          g_text_plane[last] == 0x6543 && g_attr_plane[last] == 0x87,
          "last column rejects wide replacement without writes");
    check(g_ime_render_tvram.putw(TVRAM_COLS - 2, y, 0x3042, ATTR_GREEN) == 2,
          "penultimate column accepts wide replacement");
    check(g_ime_render_tvram.putw(TVRAM_COLS - 1, y, 'Z', ATTR_CYAN) == 1 &&
          g_text_plane[last] == 'Z' && (u8)g_attr_plane[last] == ATTR_CYAN,
          "last column accepts ANK");
    check(g_ime_render_tvram.putw(-1, y, 'A', ATTR_WHITE) == 0 &&
          g_ime_render_tvram.putw(TVRAM_COLS, y, 'A', ATTR_WHITE) == 0 &&
          g_ime_render_tvram.putw(0, -1, 'A', ATTR_WHITE) == 0 &&
          g_ime_render_tvram.putw(0, TVRAM_ROWS, 'A', ATTR_WHITE) == 0,
          "renderer rejects out of range positions");
}
static int geometry_draws, geometry_clears;
static void geometry_putc(int x, int y, char c, u8 color)
{
    (void)c; (void)color;
    check(x >= 0 && x < host_cols && y == host_row, "private geometry drawing bounds");
    geometry_draws++;
}
static int geometry_putw(int x, int y, u32 c, u8 color)
{ geometry_putc(x, y, (char)c, color); return 1; }
static void geometry_clear(int y, u8 color)
{ (void)color; check(y >= host_row - 3 && y <= host_row, "private geometry row bounds"); geometry_clears++; }
static const IME_Render geometry_render = {geometry_putc, geometry_putw, geometry_clear, 0, 0};
static void geometry_case(void)
{
    host_cols = 40; host_row = 17;
    g_ime.render = &geometry_render;
    check(draw_utf8(38, 17, "abcd", ATTR_WHITE) == 2 && geometry_draws == 2,
          "private geometry limits UTF8 columns");
    g_ime.mode = IME_MODE_HIRAGANA;
    g_ime.converting = 0;
    g_ime.kana_len = 0;
    g_ime.rk.preedit[0] = 0;
    preedit_draw();
    check(geometry_clears == 1 && geometry_draws == 5, "private geometry preedit last row");
    g_cand_save_text = NULL; g_cand_save_attr = NULL;
    g_cand_has_saved = 0;
    candlist_save();
    check(g_cand_has_saved, "non TVRAM save tracks logical state without buffers");
    candlist_clear();
    check(!g_cand_has_saved && geometry_clears == 4, "non TVRAM clear uses renderer rows");
    host_cols = 80; host_row = 24;
}
int main(void)
{
    int x;
    resource_cases();
    con_output_register(&capture_output);
    tvram_clear();
    renderer_edges();
    tvram_clear();
    console_set_cursor(0, TVRAM_ROWS - 1);
    console_write("OS32:/$ x", 9, ATTR_GREEN);
    for (x = 0; x < TVRAM_COLS; x++)
        tvram_readchar_at(x, TVRAM_ROWS - 1, &before_text[x], &before_attr[x]);
    tvram_set_scroll_reserve(1);
    check(cursor_x == 9 && cursor_y == TVRAM_ROWS - 2, "IME reservation preserves cursor");
    for (x = 0; x < TVRAM_COLS; x++) {
        u16 code; u8 attr;
        tvram_readchar_at(x, TVRAM_ROWS - 2, &code, &attr);
        check(code == before_text[x] && attr == before_attr[x], "IME reservation preserves text and attributes");
    }
    g_ime.render = &g_ime_render_tvram;
    g_ime.mode = IME_MODE_HIRAGANA;
    g_ime.per_page = 9;
    g_ime.state = IME_ST_CONVERT;
    g_ime.converting = 1;
    g_ime.result_count = 1;
    g_ime.results[0].kanji[0] = 'K';
    g_ime.results[0].kanji[1] = 0;
    g_cand_save_text = saved_text; g_cand_save_attr = saved_attr;
    preedit_draw();
    for (x = 0; x < 3 * TVRAM_COLS; x++)
        tvram_readchar_at(x % TVRAM_COLS, TVRAM_ROWS - 4 + x / TVRAM_COLS,
                          &window_text[x], &window_attr[x]);
    ime_candidate_key(0, KEY_XFER);
    check(ime_candidate_key('a', 0) == 1, "candidate committed");
    for (x = 0; x < 3 * TVRAM_COLS; x++) {
        u16 code; u8 attr;
        tvram_readchar_at(x % TVRAM_COLS, TVRAM_ROWS - 4 + x / TVRAM_COLS, &code, &attr);
        check(code == window_text[x] && attr == window_attr[x], "candidate restores text and attributes");
    }
    for (x = 0; x < TVRAM_COLS; x++) {
        u16 code; u8 attr;
        tvram_readchar_at(x, TVRAM_ROWS - 1, &code, &attr);
        check(code == TVRAM_SPACE && attr == ATTR_WHITE, "preedit cleared");
    }
    check(g_ime.commit_buf[0] == 'K' && g_ime.commit_buf[1] == 'a', "commit order");
    shell_putchar(g_ime.commit_buf[0], ATTR_WHITE);
    shell_putchar(g_ime.commit_buf[1], ATTR_WHITE);
    check(cursor_y == TVRAM_ROWS - 2 && cursor_x == 11, "commit stays on prompt line");
    check(g_text_plane[(TVRAM_ROWS - 2) * TVRAM_COLS + 9] == 'K' &&
          g_text_plane[(TVRAM_ROWS - 2) * TVRAM_COLS + 10] == 'a', "commit visible at cursor");
    /* Survey 7: restore CUI rows before releasing candidate-list protection. */
    console_set_cursor(0, 0);
    for (x = 0; x < 3 * TVRAM_COLS; x++) {
        window_text[x] = (u16)(0x2100 + x);
        window_attr[x] = (u8)(x * 37 + 1);
        g_text_plane[(TVRAM_ROWS - 4) * TVRAM_COLS + x] = window_text[x];
        g_attr_plane[(TVRAM_ROWS - 4) * TVRAM_COLS + x] = window_attr[x];
    }
    g_ime.dict_loaded = 1;
    g_ime.state = IME_ST_CONVERT;
    g_ime.converting = 1;
    g_ime.result_count = 1;
    ime_candidate_key(0, KEY_DOWN);
    check(g_cand_has_saved && g_ime.state == IME_ST_CANDLIST, "mode change list opened");
    ime_set_mode(IME_MODE_KATAKANA);
    for (x = 0; x < 3 * TVRAM_COLS; x++) {
        u16 code; u8 attr;
        tvram_readchar_at(x % TVRAM_COLS, TVRAM_ROWS - 4 + x / TVRAM_COLS, &code, &attr);
        check(code == window_text[x] && attr == window_attr[x], "mode change restores candidate rows");
    }
    check(!g_cand_has_saved && !g_ime.converting && g_ime.state == IME_ST_INPUT,
          "mode change releases saved candidate rows");
    console_set_cursor(0, TVRAM_ROWS - 4);
    console_write("NEW", 3, ATTR_YELLOW);
    ime_set_mode(IME_MODE_OFF);
    for (x = 0; x < 3; x++) {
        u16 code; u8 attr;
        tvram_readchar_at(x, TVRAM_ROWS - 4, &code, &attr);
        check(code == (u8)"NEW"[x] && attr == ATTR_YELLOW,
              "OFF preserves console output after mode change");
    }
    /* Numeric selection, Return and other keys share the same restoration,
     * including the longest-match tail and the empty-preedit case. */
    for (int kind = 0; kind < 4; kind++) {
        for (int tail = 0; tail < 2; tail++) {
            g_ime.mode = IME_MODE_HIRAGANA;
            g_ime.state = IME_ST_CONVERT;
            g_ime.converting = 1;
            g_ime.candidate_idx = 0;
            g_ime.result_count = 1;
            g_ime.kana_buf[0] = 'x'; g_ime.kana_buf[1] = 'y'; g_ime.kana_buf[2] = 0;
            g_ime.kana_len = 2;
            g_ime.convert_len = tail ? 1 : 2;
            g_ime.results[0].kanji[0] = 'K'; g_ime.results[0].kanji[1] = 0;
            ime_candidate_key(0, KEY_DOWN);
            check(g_cand_has_saved, "confirmation list saved");
            int result = ime_candidate_key(kind == 0 ? '1' : kind == 2 ? 'a' : 9,
                                           kind == 1 ? KEY_RETURN : kind == 3 ? KEY_TAB : 0);
            check(result == (kind == 3 ? -1 : 1), "confirmation preserves key result");
            check(g_ime.state == IME_ST_INPUT && !g_ime.converting &&
                  !g_ime.result_count && !g_ime.convert_len && !g_cand_has_saved,
                  "confirmation clears candidate state and saved rows");
            check(g_ime.kana_len == tail && g_ime.kana_buf[tail] == 0 &&
                  (!tail || g_ime.kana_buf[0] == 'y'), "confirmation preserves remaining kana");
            check(g_ime.commit_len == (kind == 2 ? 2 : 1) && g_ime.commit_pos == 0 &&
                  g_ime.commit_buf[0] == 'K' && (kind != 2 || g_ime.commit_buf[1] == 'a'),
                  "confirmation candidate before printable key");
            u16 code; u8 attr;
            tvram_readchar_at(0, TVRAM_ROWS - 1, &code, &attr);
            check(code == (tail ? '[' : TVRAM_SPACE), "confirmation redraws remaining kana or clears preedit");
            tvram_readchar_at(4, TVRAM_ROWS - 1, &code, &attr);
            check(code == (tail ? 'y' : TVRAM_SPACE), "confirmation redraws remaining kana or clears preedit");
        }
    }
    geometry_case();
    if (failures) die(1);
    report("GREEN IME display\n"); die(0); return 0;
}
void _start(void) { main(); }
