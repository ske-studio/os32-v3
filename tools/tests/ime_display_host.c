/* Real console, TVRAM renderer and candidate restoration on host planes. */
#define main con_sink_fixture_main
#define _start con_sink_fixture_start
#include "con_sink_host.c"
#undef main
#undef _start
#include "../../kernel/ime/ime_render_tvram.c"
#include "../../kernel/ime/ime.c"

void *kmemcpy(void *dst, const void *src, u32 n)
{ u8 *d = dst; const u8 *s = src; while (n--) *d++ = *s++; return dst; }
void ime_dict_learn(IME_Dict *dict, const char *yomi, const char *kanji)
{ (void)dict; (void)yomi; (void)kanji; }
int services_public = 1;
int ime_dict_open(IME_Dict *dict, const char *path)
{ (void)dict; (void)path; return 0; }
void __cdecl kprintf(u8 color, const char *fmt, ...)
{ (void)color; (void)fmt; }
static u16 saved_text[3 * TVRAM_COLS];
static u8 saved_attr[3 * TVRAM_COLS];
static u16 before_text[TVRAM_COLS];
static u8 before_attr[TVRAM_COLS];
static u16 window_text[3 * TVRAM_COLS];
static u8 window_attr[3 * TVRAM_COLS];
int main(void)
{
    int x;
    con_output_register(&capture_output);
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
        window_text[x] = 'A' + x % 26;
        window_attr[x] = ATTR_GREEN;
        tvram_putchar_at(x % TVRAM_COLS, TVRAM_ROWS - 4 + x / TVRAM_COLS,
                         (char)window_text[x], window_attr[x]);
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
    if (failures) die(1);
    report("GREEN IME display\n"); die(0); return 0;
}
void _start(void) { main(); }
