/* C1: the real kernel/console/console.c (generated include = product text) on a
 * recording backend whose grid is not 80x25. Out-of-grid access exits 98/99. */
#include "types.h"
#include "console_backend.h"

static void finish(int rc) __attribute__((noreturn));
static void finish(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
#define MAX_COLS 132
#define MAX_ROWS 50
static struct con_geometry grid;
static u16 cells[MAX_ROWS][MAX_COLS];
static u8 attrs[MAX_ROWS][MAX_COLS];
static int cur_x, cur_y, scrolls, scroll_rows, clears, backend_calls, safe = 1;
static int sink_x, sink_y;

static void inside(int x, int y, int width)
{
    if (x < 0 || y < 0 || x + width > grid.cols || y >= grid.rows) finish(99);
}
const struct con_geometry *con_backend_geometry(void) { return &grid; }
u8 con_backend_color(unsigned role) { return role == CON_COLOR_DEFAULT ? 0x5A : 0; }
int con_backend_safe(void) { return safe; }
void con_backend_clear(void) { backend_calls++; clears++; }
void con_backend_scroll(int rows) { backend_calls++; scrolls++; scroll_rows = rows; }
void con_backend_putchar(int x, int y, char ch, u8 raw_attr)
{
    backend_calls++; inside(x, y, 1);
    cells[y][x] = (u8)ch; attrs[y][x] = raw_attr;
}
void con_backend_readchar(int x, int y, u16 *code, u8 *attr)
{
    backend_calls++; inside(x, y, 1);
    *code = cells[y][x]; *attr = attrs[y][x];
}
void con_backend_putkanji(int x, int y, u16 jis, u8 raw_attr)
{
    backend_calls++; inside(x, y, CON_WIDE_COLS);
    cells[y][x] = jis; cells[y][x + 1] = 0xFFFF; attrs[y][x] = raw_attr;
}
int con_backend_reverse(int x, int y) { backend_calls++; inside(x, y, 1); return 1; }
void con_backend_cursor_at(int x, int y)
{
    backend_calls++;
    if (x < 0 || y < 0 || x >= grid.cols || y >= grid.rows) finish(98);
    cur_x = x; cur_y = y;
}
void con_backend_cursor_shape(int visible) { (void)visible; backend_calls++; }

/* The rest of the console's world: inert. */
int con_dual_active(void) { return 0; }
int con_dual_was_live(void) { return 0; }
void con_dual_session_end(void) { }
int con_sink_is_enabled(void) { return 0; }
void con_sink_enable(void) { }
void con_sink_disable(void) { }
void con_sink_push_clear(void) { }
void con_sink_push_cursor(int x, int y) { sink_x = x; sink_y = y; }
void con_sink_push_print(const char *s, u32 n, u8 color) { (void)s; (void)n; (void)color; }
void bootlog_push(const char *s, u32 n) { (void)s; (void)n; }
void gfx_policy_reeval(void) { }
void kbd_inject_discard(void) { }
int con_output_putchar(char ch) { (void)ch; return 0; }
int legacy_active(void) { return 0; }
/* lib/utf8.c (product) decodes and maps; only the font-file table is replaced. */
u16 unicode_file_lookup(u32 cp) { return cp == 0x3042 ? 0x2422 : 0; }
u32 kstrlen(const char *s) { u32 n = 0; while (s[n]) n++; return n; }
int kutoa_dec(u32 val, char *buf, int bufsz) { (void)val; (void)bufsz; buf[0] = 0; return 0; }

#include "console_source.inc"

static void at(int x, int y) { console_set_cursor(x, y); }
static void run(int g, int cols, int rows)
{
    int w = -1, h = -1, i, before;
    char line[MAX_COLS + 2];

    grid.cols = cols; grid.rows = rows;
    tvram_set_scroll_reserve(0);
    tvram_clear();
    if (clears != g + 1 || cur_x || cur_y) finish(5 + g);

    console_get_size(&w, &h);
    if (w != cols || h != rows) finish(10 + g);

    /* A full line wraps exactly at the backend's width. */
    for (i = 0; i < cols; i++) line[i] = 'a';
    line[cols] = 'b'; line[cols + 1] = 0;
    shell_print(line, 0x21);
    if (cells[0][cols - 1] != 'a' || cells[1][0] != 'b' || cur_x != 1 || cur_y != 1) finish(20 + g);

    at(cols + 7, rows + 7);
    if (cur_x != cols - 1 || cur_y != rows - 1 || sink_x != cols - 1 || sink_y != rows - 1) finish(30 + g);
    at(-3, -3);
    if (cur_x || cur_y) finish(30 + g);

    /* A newline on the last row scrolls the whole grid and stays on that row. */
    at(0, rows - 1); scrolls = 0;
    shell_print("\n", 0x21);
    if (scrolls != 1 || scroll_rows != rows || cur_x != 0 || cur_y != rows - 1) finish(40 + g);

    /* Reserved rows are clamped to the grid and excluded from scrolling. */
    tvram_set_scroll_reserve(rows + 3);
    scrolls = 0; tvram_scroll();
    if (scrolls != 1 || scroll_rows != 1) finish(50 + g);
    tvram_set_scroll_reserve(0);
    at(0, rows - 1); scrolls = 0;
    tvram_set_scroll_reserve(1);        /* first reservation moves the input line up */
    if (scrolls != 1 || scroll_rows != rows || cur_y != rows - 2) finish(54 + g);
    scrolls = 0;
    shell_print("\n", 0x21);
    if (scrolls != 1 || scroll_rows != rows - 1 || cur_y != rows - 2) finish(58 + g);
    tvram_set_scroll_reserve(0);

    /* A wide character never starts in the last column. */
    at(cols - 1, 0);
    shell_print_utf8("\xE3\x81\x82", 0x41);
    if (cells[1][0] != 0x2422 || cur_x != CON_WIDE_COLS || cur_y != 1) finish(60 + g);
    at(cols - 2, 2);
    shell_print_utf8("\xE3\x81\x82", 0x41);
    if (cells[2][cols - 2] != 0x2422 || cur_x != 0 || cur_y != 3) finish(64 + g);
    /* An unconvertible code point is the JIS replacement, also kept inside the row. */
    at(cols - 1, 4);
    shell_print_utf8("\xF0\x9F\x98\x80", 0x41);
    if (cells[5][0] != CON_JIS_REPLACEMENT || cur_x != CON_WIDE_COLS || cur_y != 5) finish(70 + g);

    /* Backspace erases with the backend's default colour, not the caller's. */
    at(3, 6);
    shell_putchar('\b', 0x11);
    if (cells[6][2] != ' ' || attrs[6][2] != 0x5A || cur_x != 2) finish(80 + g);

    /* While the output is routed elsewhere nothing reaches the backend. */
    safe = 0; before = backend_calls;
    at(1, 1);
    shell_print("zz", 0x21); shell_putchar('q', 0x21); shell_print_utf8("\xE3\x81\x82", 0x41);
    tvram_clear(); tvram_scroll(); tvram_putchar_at(0, 0, 'x', 1); tvram_putkanji_at(0, 0, 0x2422, 1);
    {
        u16 code = 0x1234; u8 attr = 0x56;
        tvram_readchar_at(0, 0, &code, &attr);
        if (code != 0x1234 || attr != 0x56 || tvram_reverse_cell(0, 0) != 0) finish(90 + g);
    }
    console_hw_cursor_enable(); console_text_gdc_stop();
    safe = 1;
    if (backend_calls != before) finish(90 + g);
    /* ...and the logical cursor did not advance behind the frozen screen. */
    shell_print("", 0x21);
    if (cur_x != 1 || cur_y != 1) finish(94 + g);
}

void _start(void)
{
    run(0, 80, 25);
    run(1, 40, 10);
    run(2, 132, 50);
    finish(0);
}
