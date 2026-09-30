/* ======================================================================== */
/*  FILER_DRAW.C — TVRAMファイラー描画ライブラリ                             */
/*                                                                          */
/*  cmd_filer.c から分離された描画関数群。                                   */
/*  PC-98 TVRAM直接書き込みでファイルリスト表示を行う。                      */
/* ======================================================================== */

#include <string.h>
#include "filer_draw.h"

static KernelAPI *fd_api;

/* ポップアップ表示時間 (約2秒) */
#define FL_POPUP_TICKS  200

/* ======================================================================== */
/*  初期化                                                                  */
/* ======================================================================== */

void fldraw_init(KernelAPI *api)
{
    fd_api = api;
}

/* ======================================================================== */
/*  基本描画                                                                */
/* ======================================================================== */

void fldraw_clear_line(int y, u8 attr)
{
    int x;
    for (x = 0; x < FL_SCREEN_COLS; x++)
        fd_api->tvram_putchar_at(x, y, ' ', attr);
}

void fldraw_str(int x, int y, const char *s, int max_len, u8 attr)
{
    int max_x = x + max_len;
    if (max_x > FL_SCREEN_COLS) max_x = FL_SCREEN_COLS;
    while (*s && x < max_x) {
        fd_api->tvram_putchar_at(x, y, *s, attr);
        s++;
        x++;
    }
}

void fldraw_number(int x, int y, u32 val, u8 attr)
{
    char buf[12];
    int len = 0;
    int i;

    if (val == 0) {
        fd_api->tvram_putchar_at(x, y, '0', attr);
        return;
    }
    while (val > 0 && len < 11) {
        buf[len++] = '0' + (val % 10);
        val /= 10;
    }
    for (i = len - 1; i >= 0; i--) {
        fd_api->tvram_putchar_at(x, y, buf[i], attr);
        x++;
    }
}

void fldraw_size_str(int x, int y, u32 size, u8 attr)
{
    char buf[16];
    int len = 0;
    u32 val;

    if (size >= 1048576) {
        val = size / 1048576;
        while (val > 0 && len < 10) { buf[len++] = '0' + (val % 10); val /= 10; }
        { int i; for (i = len - 1; i >= 0; i--) fd_api->tvram_putchar_at(x++, y, buf[i], attr); }
        fldraw_str(x, y, " MB", 3, attr);
    } else if (size >= 1024) {
        val = size / 1024;
        while (val > 0 && len < 10) { buf[len++] = '0' + (val % 10); val /= 10; }
        { int i; for (i = len - 1; i >= 0; i--) fd_api->tvram_putchar_at(x++, y, buf[i], attr); }
        fldraw_str(x, y, " KB", 3, attr);
    } else {
        val = size;
        if (val == 0) {
            fd_api->tvram_putchar_at(x++, y, '0', attr);
        } else {
            while (val > 0 && len < 10) { buf[len++] = '0' + (val % 10); val /= 10; }
            { int i; for (i = len - 1; i >= 0; i--) fd_api->tvram_putchar_at(x++, y, buf[i], attr); }
        }
        fldraw_str(x, y, " B", 2, attr);
    }
}

/* ======================================================================== */
/*  レイアウト描画                                                          */
/* ======================================================================== */

void fldraw_header(const FL_State *st)
{
    char count_buf[16];
    int n, ci, x;

    fldraw_clear_line(FL_HEADER_Y, FL_ATTR_HEADER);
    fldraw_str(0, FL_HEADER_Y, "[Filer] ", 8, FL_ATTR_HEADER);
    fldraw_str(8, FL_HEADER_Y, st->cwd, FL_SCREEN_COLS - 20, FL_ATTR_HEADER);

    n = st->count;
    ci = 0;
    if (n >= 100) count_buf[ci++] = '0' + (n / 100) % 10;
    if (n >= 10)  count_buf[ci++] = '0' + (n / 10) % 10;
    count_buf[ci++] = '0' + (n % 10);
    count_buf[ci++] = ' ';
    count_buf[ci++] = 'i'; count_buf[ci++] = 't'; count_buf[ci++] = 'e';
    count_buf[ci++] = 'm'; count_buf[ci++] = 's';
    if (st->dropped > 0) { count_buf[ci++] = '+'; }
    count_buf[ci] = '\0';
    x = FL_SCREEN_COLS - ci - 1;
    fldraw_str(x, FL_HEADER_Y, count_buf, ci, FL_ATTR_STATUS);

    /* 載せられなかった件数は隠さずに出す (fl_ls_callback が名前を切らずに
     * 捨てた分。切った名前を一覧に出すと別のファイルを起動できてしまう)。 */
    if (st->dropped > 0) {
        fldraw_str(x - 12, FL_HEADER_Y, "(hidden ", 8, FL_ATTR_STATUS);
        fldraw_number(x - 4, FL_HEADER_Y, (u32)st->dropped, FL_ATTR_STATUS);
    }
}

void fldraw_separator(void)
{
    int x;
    for (x = 0; x < FL_SCREEN_COLS; x++)
        fd_api->tvram_putchar_at(x, FL_SEPARATOR_Y, '-', FL_ATTR_SEPARATOR);
}

void fldraw_entry(int col_x, int y, const FL_Entry *e, int selected)
{
    u8 attr;
    u8 bg_attr;
    int x;
    const char *name;
    int max_name;

    if (selected) {
        if (e->is_dir) attr = FL_ATTR_DIR_SEL;
        else if (e->is_exe) attr = FL_ATTR_EXE_SEL;
        else attr = FL_ATTR_SELECTED;
        bg_attr = attr;
    } else {
        if (e->is_dir) attr = FL_ATTR_DIR;
        else if (e->is_exe) attr = FL_ATTR_EXE;
        else attr = FL_ATTR_FILE;
        bg_attr = FL_ATTR_FILE;
    }

    for (x = col_x; x < col_x + FL_COL_WIDTH && x < FL_SCREEN_COLS; x++)
        fd_api->tvram_putchar_at(x, y, ' ', bg_attr);

    x = col_x + 1;
    max_name = FL_COL_WIDTH - 2;

    if (e->is_dir) {
        fd_api->tvram_putchar_at(x, y, '/', attr);
        x++;
        max_name--;
    }

    name = e->name;
    while (*name && max_name > 0 && x < FL_SCREEN_COLS) {
        fd_api->tvram_putchar_at(x, y, *name, attr);
        name++;
        x++;
        max_name--;
    }
}

void fldraw_entry_at(const FL_State *st, int idx, int selected)
{
    int page_idx, col, row, col_x, y;

    if (idx < st->page_top || idx >= st->page_top + FL_PAGE_ITEMS) return;
    if (idx >= st->count) return;

    page_idx = idx - st->page_top;
    row = page_idx / FL_NUM_COLUMNS;
    col = page_idx % FL_NUM_COLUMNS;
    col_x = col * FL_COL_WIDTH;
    y = FL_LIST_TOP_Y + row;

    fldraw_entry(col_x, y, &st->entries[idx], selected);
}

void fldraw_list(const FL_State *st)
{
    int i, idx;
    int col, row;
    int col_x, y;

    for (i = 0; i < FL_LIST_ROWS; i++)
        fldraw_clear_line(FL_LIST_TOP_Y + i, FL_ATTR_FILE);

    for (i = 0; i < FL_PAGE_ITEMS; i++) {
        idx = st->page_top + i;
        if (idx >= st->count) break;

        row = i / FL_NUM_COLUMNS;
        col = i % FL_NUM_COLUMNS;
        col_x = col * FL_COL_WIDTH;
        y = FL_LIST_TOP_Y + row;

        fldraw_entry(col_x, y, &st->entries[idx], (idx == st->cursor));
    }
}

void fldraw_footer(const FL_State *st)
{
    const FL_Entry *e;

    fldraw_clear_line(FL_FOOTER_Y, FL_ATTR_FOOTER);

    if (st->count == 0) {
        fldraw_str(2, FL_FOOTER_Y, "(empty directory)", 20, FL_ATTR_FOOTER);
        return;
    }

    e = &st->entries[st->cursor];

    if (e->is_dir) {
        fldraw_str(2, FL_FOOTER_Y, "Dir: ", 5, FL_ATTR_FOOTER);
        fldraw_str(7, FL_FOOTER_Y, e->name, 30, FL_ATTR_DIR);
    } else {
        fldraw_str(2, FL_FOOTER_Y, "File: ", 6, FL_ATTR_FOOTER);
        fldraw_str(8, FL_FOOTER_Y, e->name, 30, FL_ATTR_FOOTER);
        fldraw_str(42, FL_FOOTER_Y, "Size: ", 6, FL_ATTR_FOOTER);
        fldraw_size_str(48, FL_FOOTER_Y, e->size, FL_ATTR_STATUS);
    }

    {
        int pos_x = 68;
        fldraw_number(pos_x, FL_FOOTER_Y, (u32)(st->cursor + 1), FL_ATTR_STATUS);
        {
            int digits = 1;
            u32 tmp = (u32)(st->cursor + 1);
            while (tmp >= 10) { digits++; tmp /= 10; }
            pos_x += digits;
        }
        fd_api->tvram_putchar_at(pos_x, FL_FOOTER_Y, '/', FL_ATTR_FOOTER);
        pos_x++;
        fldraw_number(pos_x, FL_FOOTER_Y, (u32)st->count, FL_ATTR_STATUS);
    }
}

void fldraw_help(const FL_State *st)
{
    int total_pages, current_page;
    char page_buf[16];
    int pi, x;

    fldraw_clear_line(FL_HELP_Y, FL_ATTR_HELP);
    fldraw_str(0, FL_HELP_Y, "[Enter]Open  [BS]Parent  [Q]Quit", 40, FL_ATTR_HELP);

    if (st->count > FL_PAGE_ITEMS) {
        total_pages = (st->count + FL_PAGE_ITEMS - 1) / FL_PAGE_ITEMS;
        current_page = (st->page_top / FL_PAGE_ITEMS) + 1;
        pi = 0;
        page_buf[pi++] = 'P'; page_buf[pi++] = 'a'; page_buf[pi++] = 'g';
        page_buf[pi++] = 'e'; page_buf[pi++] = ' ';
        if (current_page >= 10) page_buf[pi++] = '0' + (current_page / 10);
        page_buf[pi++] = '0' + (current_page % 10);
        page_buf[pi++] = '/';
        if (total_pages >= 10) page_buf[pi++] = '0' + (total_pages / 10);
        page_buf[pi++] = '0' + (total_pages % 10);
        page_buf[pi] = '\0';
        x = FL_SCREEN_COLS - pi - 1;
        fldraw_str(x, FL_HELP_Y, page_buf, pi, FL_ATTR_STATUS);
    }
}

void fldraw_all(const FL_State *st)
{
    fldraw_header(st);
    fldraw_separator();
    fldraw_list(st);
    fldraw_footer(st);
    fldraw_help(st);
}

void fldraw_cursor_update(const FL_State *st, int old_cursor)
{
    fldraw_entry_at(st, old_cursor, 0);
    fldraw_entry_at(st, st->cursor, 1);
    fldraw_footer(st);
}

void fldraw_popup_message(const char *msg, u8 attr)
{
    u32 start;
    fldraw_clear_line(FL_FOOTER_Y, attr);
    fldraw_str(2, FL_FOOTER_Y, msg, FL_SCREEN_COLS - 4, attr);
    start = fd_api->get_tick();
    while ((fd_api->get_tick() - start) < FL_POPUP_TICKS) {
        if (fd_api->kbd_trygetkey() >= 0) break;
        fd_api->sys_halt();
    }
    /* キーバッファをフラッシュ */
    while (fd_api->kbd_trygetkey() >= 0)
        ;
}
