#include "fbcon.h"
#include "mm/kmalloc.h"

static int inside(struct fbcon *c, int x, int y)
{
    return c->ready && x >= 0 && y >= 0 &&
           x < c->geometry.cols && y < c->geometry.rows;
}

/* References: yaft terminal.h:165, fb/common.h:415; Flanterm fb.c:1069. */
static int cursor_cell(struct fbcon *c, int x, int y)
{
    struct fbcon_cell *s = &c->cells[y * c->geometry.cols + x];
    return c->visible && y == c->cursor_y &&
        (x == c->cursor_x || (s->part == 1 && x + 1 == c->cursor_x) ||
         (s->part == 2 && x - 1 == c->cursor_x));
}

static struct fbcon_cell rendered(struct fbcon *c, int x, int y)
{
    struct fbcon_cell s = c->cells[y * c->geometry.cols + x];
    s.attr = (s.attr & (1 | FBCON_REVERSE)) | (cursor_cell(c, x, y) ? 2 : 0);
    return s;
}

/* Render each half from the original glyph, including a cursor overlay. */
static void paint(struct fbcon *c, int x, int y, u8 *pixels)
{
    struct fbcon_cell *s = &c->cells[y * c->geometry.cols + x];
    u8 glyph[32];
    int row, bit, inverse = !!(s->attr & FBCON_REVERSE);
    if (s->part) c->font->read_kanji(s->code, glyph);
    else c->font->read_ank((u8)s->code, glyph);
    for (row = 0; row < 16; row++) {
        u8 mask = glyph[s->part ? row * 2 + (s->part == 2) : row];
        int cursor = cursor_cell(c, x, y) && row >= 14;
        for (bit = 0; bit < 8; bit++) {
            int ink = !!(mask & (0x80u >> bit));
            pixels[(c->top + y * 16 + row) * c->target->pitch + x * 8 + bit] =
                (((s->attr & 1) && (ink ^ inverse)) ^ cursor) ? c->fg : c->bg;
        }
    }
    c->drawn[y * c->geometry.cols + x] = rendered(c, x, y);
}

static void redraw(struct fbcon *c, int x, int y, int w, int h)
{
    u8 *pixels;
    int xx, yy;
    if (!c->ready) return;
    if (gfx_console_lock(c->target, &pixels)) { c->ready = 0; return; }
    for (yy = y; yy < y + h; yy++)
        for (xx = x; xx < x + w; xx++) paint(c, xx, yy, pixels);
    if (gfx_console_unlock(c->target, x * 8, c->top + y * 16, w * 8, h * 16) ||
        gfx_console_present(c->target)) c->ready = 0;
}

int fbcon_attach(struct fbcon *c, struct gfx_console_target *t,
                 const KcgModuleApi *font, unsigned top_pixels)
{
    u32 cols, rows;
    if (!c || c->cells || !t || !font || !font->read_ank || !font->read_kanji ||
        top_pixels >= t->height || t->pitch < t->width) return -1;
    cols = t->width / 8;
    rows = (t->height - top_pixels) / 16;
    if (!cols || !rows || cols > 0x7fffffffu / rows / (2 * sizeof(*c->cells))) return -1;
    c->cells = kmalloc(2 * cols * rows * sizeof(*c->cells));
    if (!c->cells) return -1;
    c->drawn = c->cells + cols * rows;
    c->target = t;
    c->font = font;
    c->geometry = (struct con_geometry){cols, rows};
    c->top = top_pixels;
    c->cursor_x = c->cursor_y = c->visible = 0;
    c->fg = 15; c->bg = 0;
    c->ready = 1;
    fbcon_clear(c);
    if (!c->ready) { fbcon_detach(c); return -1; }
    return 0;
}

void fbcon_set_colors(struct fbcon *c, u8 fg, u8 bg)
{
    c->fg = fg; c->bg = bg;
    redraw(c, 0, 0, c->geometry.cols, c->geometry.rows);
}

void fbcon_detach(struct fbcon *c)
{
    kfree(c->cells);
    *c = (struct fbcon){0};
}

void fbcon_clear(struct fbcon *c)
{
    int i;
    if (!c->ready) return;
    for (i = 0; i < c->geometry.cols * c->geometry.rows; i++)
        c->cells[i] = (struct fbcon_cell){' ', FBCON_DEFAULT_ATTR, 0};
    redraw(c, 0, 0, c->geometry.cols, c->geometry.rows);
}

/* rows is the scrollable prefix; reserved bottom rows remain untouched. */
void fbcon_scroll_n(struct fbcon *c, int rows, int n)
{
    int i, x, y, cols = c->geometry.cols;
    u8 *pixels = 0;
    if (!c->ready || rows < 1 || rows > c->geometry.rows || n < 1) return;
    if (n > rows) n = rows;
    for (i = 0; i < cols * (rows - n); i++) c->cells[i] = c->cells[i + cols * n];
    for (; i < cols * rows; i++)
        c->cells[i] = (struct fbcon_cell){' ', FBCON_DEFAULT_ATTR, 0};
    for (y = 0; y < rows; y++) for (x = 0; x < cols; x++) {
        struct fbcon_cell next = rendered(c, x, y);
        struct fbcon_cell old = c->drawn[y * cols + x];
        if (next.code == old.code && next.attr == old.attr && next.part == old.part) continue;
        if (!pixels && gfx_console_lock(c->target, &pixels)) { c->ready = 0; return; }
        paint(c, x, y, pixels);
    }
    if (pixels && (gfx_console_unlock(c->target, 0, c->top, cols * 8, rows * 16) ||
                   gfx_console_present(c->target))) c->ready = 0;
}

void fbcon_scroll(struct fbcon *c, int rows) { fbcon_scroll_n(c, rows, 1); }

/* Erase an overlapping wide glyph as a pair before replacing its cells. */
static void unpair(struct fbcon *c, int x, int y)
{
    struct fbcon_cell *s = &c->cells[y * c->geometry.cols + x];
    if (s->part == 2) x--;
    else if (s->part != 1) return;
    c->cells[y * c->geometry.cols + x] = (struct fbcon_cell){' ', FBCON_DEFAULT_ATTR, 0};
    c->cells[y * c->geometry.cols + x + 1] = (struct fbcon_cell){' ', FBCON_DEFAULT_ATTR, 0};
}

void fbcon_putchar(struct fbcon *c, int x, int y, u16 code, u8 attr, int wide)
{
    int left, right;
    if (!inside(c, x, y) || (wide && x + 1 >= c->geometry.cols)) return;
    left = x > 0 ? x - 1 : x;
    right = x + (wide ? 2 : 1);
    if (right < c->geometry.cols) right++;
    unpair(c, x, y);
    if (wide) unpair(c, x + 1, y);
    c->cells[y * c->geometry.cols + x] = (struct fbcon_cell){code, attr, wide ? 1 : 0};
    if (wide) c->cells[y * c->geometry.cols + x + 1] = (struct fbcon_cell){code, attr, 2};
    redraw(c, left, y, right - left, 1);
}

void fbcon_readchar(struct fbcon *c, int x, int y, u16 *code, u8 *attr)
{
    struct fbcon_cell *s;
    if (!inside(c, x, y)) return;
    s = &c->cells[y * c->geometry.cols + x];
    if (code) *code = s->code;
    if (attr) *attr = s->attr;
}

int fbcon_reverse(struct fbcon *c, int x, int y)
{
    int w, i;
    if (!inside(c, x, y)) return 0;
    if (c->cells[y * c->geometry.cols + x].part == 2) x--;
    w = c->cells[y * c->geometry.cols + x].part ? 2 : 1;
    for (i = 0; i < w; i++) c->cells[y * c->geometry.cols + x + i].attr ^= FBCON_REVERSE;
    redraw(c, x, y, w, 1);
    return w;
}

static void redraw_cursor(struct fbcon *c, int x, int y)
{
    int w;
    if (!inside(c, x, y)) return;
    if (c->cells[y * c->geometry.cols + x].part == 2) x--;
    w = c->cells[y * c->geometry.cols + x].part ? 2 : 1;
    redraw(c, x, y, w, 1);
}

void fbcon_cursor_at(struct fbcon *c, int x, int y)
{
    int ox = c->cursor_x, oy = c->cursor_y;
    if (!c->ready) return;
    if (x < 0) x = 0;
    if (x >= c->geometry.cols) x = c->geometry.cols - 1;
    if (y < 0) y = 0;
    if (y >= c->geometry.rows) y = c->geometry.rows - 1;
    if (x == ox && y == oy) return;
    c->cursor_x = x; c->cursor_y = y;
    if (!c->visible) return;
    redraw_cursor(c, ox, oy);
    redraw_cursor(c, x, y);
}

void fbcon_cursor_shape(struct fbcon *c, int visible)
{
    if (c->visible == !!visible) return;
    c->visible = !!visible;
    redraw_cursor(c, c->cursor_x, c->cursor_y);
}
