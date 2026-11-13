#include "fbcon.h"
#include "mm/kmalloc.h"

static int inside(struct fbcon *c, int x, int y)
{
    return c->ready && x >= 0 && y >= 0 &&
           x < c->geometry.cols && y < c->geometry.rows;
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
        int cursor = c->visible && x == c->cursor_x && y == c->cursor_y && row >= 14;
        for (bit = 0; bit < 8; bit++) {
            int ink = !!(mask & (0x80u >> bit));
            pixels[(c->top + y * 16 + row) * c->target->pitch + x * 8 + bit] =
                (s->attr & 1) && (ink ^ inverse ^ cursor) ? c->fg : c->bg;
        }
    }
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
    if (!cols || !rows || cols > 0x7fffffffu / rows / sizeof(*c->cells)) return -1;
    c->cells = kmalloc(cols * rows * sizeof(*c->cells));
    if (!c->cells) return -1;
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
void fbcon_scroll(struct fbcon *c, int rows)
{
    int i, cols = c->geometry.cols, visible = c->visible;
    if (!c->ready || rows < 1 || rows > c->geometry.rows) return;
    fbcon_cursor_shape(c, 0);
    for (i = 0; i < cols * (rows - 1); i++) c->cells[i] = c->cells[i + cols];
    for (; i < cols * rows; i++)
        c->cells[i] = (struct fbcon_cell){' ', FBCON_DEFAULT_ATTR, 0};
    redraw(c, 0, 0, cols, rows);
    fbcon_cursor_shape(c, visible);
}

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

void fbcon_cursor_at(struct fbcon *c, int x, int y)
{
    int ox = c->cursor_x, oy = c->cursor_y;
    c->cursor_x = x; c->cursor_y = y;
    if (inside(c, ox, oy)) redraw(c, ox, oy, 1, 1);
    if (inside(c, x, y)) redraw(c, x, y, 1, 1);
}

void fbcon_cursor_shape(struct fbcon *c, int visible)
{
    c->visible = !!visible;
    if (inside(c, c->cursor_x, c->cursor_y)) redraw(c, c->cursor_x, c->cursor_y, 1, 1);
}
