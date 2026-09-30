/* ======================================================================== */
/*  MD_RENDER.C — Markdown描画エンジン                                       */
/*                                                                          */
/*  mdview.cから分離された描画ロジック。                                     */
/*  ワードラップ、レイアウト計算、ノード描画、ページ描画を担当。             */
/* ======================================================================== */

#include <stdio.h>
#include <string.h>
#include "md_render.h"
#include "libos32gfx.h"

static KernelAPI *r_api;

/* ======================================================================== */
/*  定数 (内部用)                                                            */
/* ======================================================================== */

/* 見出しインデント */
#define INDENT_H1       32
#define INDENT_H2       24
#define INDENT_H3       16

/* フォントサイズ (KCGスケール別) */
#define FONT_H_S1       16
#define FONT_W_ANK_S1   8
#define FONT_W_KJ_S1    16

#define FONT_H_S2       32
#define FONT_H_S3       48

/* 行間・余白 */
#define TEXT_LINE_GAP   4
#define TEXT_LINE_H     (FONT_H_S1 + TEXT_LINE_GAP)

#define H1_MARGIN_TOP   12
#define H1_MARGIN_BTM   8
#define H1_BORDER_H     2

#define H2_MARGIN_TOP   10
#define H2_MARGIN_BTM   6
#define H2_BORDER_H     1

#define H3_MARGIN_TOP   6
#define H3_MARGIN_BTM   4

#define CODE_PAD_V      4
#define CODE_PAD_H      8
#define CODE_LINE_H     18

#define BLANK_H         10
#define HRULE_H         12

#define LIST_BULLET_W   12
#define TABLE_ROW_H     20
#define TABLE_PAD       4

#define QUOTE_BAR_W     3
#define QUOTE_PAD       8
#define QUOTE_MARGIN    4

/* ======================================================================== */
/*  パレット                                                                */
/* ======================================================================== */

const unsigned char md_palette[16][3] = {
    /*  R,  G,  B  (各0-15) */
    {  1,  1,  3 },  /* 0: 背景 ダークネイビー */
    { 15, 13,  4 },  /* 1: H1 ゴールド */
    {  5, 12, 15 },  /* 2: H2 スカイブルー */
    {  4, 14, 10 },  /* 3: H3 エメラルド */
    { 15, 15, 15 },  /* 4: 本文 白 */
    { 12,  9, 15 },  /* 5: コード ラベンダー */
    {  3,  3,  4 },  /* 6: コードブロック背景 */
    {  7,  7,  8 },  /* 7: 装飾線 グレー */
    { 15,  7,  6 },  /* 8: 太字 サーモン */
    {  5, 13, 15 },  /* 9: リンク シアン */
    { 10, 10, 10 },  /* 10: bullet */
    { 11,  9,  3 },  /* 11: H1バー 暗ゴールド */
    {  2,  2,  4 },  /* 12: ステータスBG */
    { 13, 13, 14 },  /* 13: ステータスFG */
    { 15, 14,  4 },  /* 14: 検索ハイライト */
    {  6, 10, 13 }   /* 15: 引用バー / テーブル交互 */
};

/* ======================================================================== */
/*  検索状態                                                                */
/* ======================================================================== */

static MdSearchState r_search;

/* ======================================================================== */
/*  ワードラップ                                                            */
/* ======================================================================== */

#define WRAP_MAX_LINES 32

typedef struct {
    const char *start[WRAP_MAX_LINES];
    int         len[WRAP_MAX_LINES];
    int         count;
} WrapResult;

static int utf8_char_bytes(const unsigned char *p)
{
    if (*p < 0x80) return 1;
    if ((*p & 0xE0) == 0xC0) return 2;
    if ((*p & 0xF0) == 0xE0) return 3;
    if ((*p & 0xF8) == 0xF0) return 4;
    return 1;
}

static int char_pixel_width(const unsigned char *p)
{
    if (*p < 0x80) return FONT_W_ANK_S1;
    return FONT_W_KJ_S1;
}

static void word_wrap(const char *text, int text_len,
                      int max_width_px, WrapResult *out)
{
    const unsigned char *p = (const unsigned char *)text;
    const unsigned char *end = p + text_len;
    const unsigned char *line_start = p;
    const unsigned char *last_space = NULL;
    int cur_width = 0;

    out->count = 0;

    while (p < end && out->count < WRAP_MAX_LINES) {
        int cw;
        int cb;

        if (*p == '\0') break;

        cb = utf8_char_bytes(p);
        cw = char_pixel_width(p);

        if (*p == ' ') {
            last_space = p;
        }

        if (cur_width + cw > max_width_px) {
            if (last_space && last_space > line_start) {
                out->start[out->count] = (const char *)line_start;
                out->len[out->count] = (int)(last_space - line_start);
                out->count++;
                p = last_space + 1;
            } else {
                out->start[out->count] = (const char *)line_start;
                out->len[out->count] = (int)(p - line_start);
                out->count++;
            }
            line_start = p;
            last_space = NULL;
            cur_width = 0;
            continue;
        }

        cur_width += cw;
        p += cb;
    }

    if (p > line_start && out->count < WRAP_MAX_LINES) {
        out->start[out->count] = (const char *)line_start;
        out->len[out->count] = (int)(p - line_start);
        out->count++;
    }

    if (out->count == 0) {
        out->start[0] = text;
        out->len[0] = 0;
        out->count = 1;
    }
}

/* ======================================================================== */
/*  レイアウト計算                                                          */
/* ======================================================================== */

static int node_y[MD_MAX_NODES];
static int node_height[MD_MAX_NODES];
static int node_code_first[MD_MAX_NODES];
static int node_code_last[MD_MAX_NODES];

int md_layout(MdDocument *doc)
{
    int y = 8;
    int i;
    WrapResult wrap;

    for (i = 0; i < doc->node_count; i++) {
        node_code_first[i] = 0;
        node_code_last[i] = 0;
    }
    for (i = 0; i < doc->node_count; i++) {
        if (doc->nodes[i].type == MD_CODE_BLOCK) {
            if (i == 0 || doc->nodes[i - 1].type != MD_CODE_BLOCK) {
                node_code_first[i] = 1;
            }
            if (i == doc->node_count - 1 || doc->nodes[i + 1].type != MD_CODE_BLOCK) {
                node_code_last[i] = 1;
            }
        }
    }

    for (i = 0; i < doc->node_count; i++) {
        MdNode *n = &doc->nodes[i];
        int h = 0;

        node_y[i] = y;

        switch (n->type) {
        case MD_H1:
            h = H1_MARGIN_TOP + FONT_H_S3 + H1_BORDER_H + H1_MARGIN_BTM;
            break;
        case MD_H2:
            h = H2_MARGIN_TOP + FONT_H_S2 + H2_BORDER_H + H2_MARGIN_BTM;
            break;
        case MD_H3:
            h = H3_MARGIN_TOP + FONT_H_S1 + H3_MARGIN_BTM;
            break;
        case MD_PARAGRAPH:
            word_wrap(n->text, n->text_len, MDR_CONTENT_W, &wrap);
            h = wrap.count * TEXT_LINE_H;
            break;
        case MD_LIST_ITEM:
            word_wrap(n->text, n->text_len,
                      MDR_CONTENT_W - LIST_BULLET_W, &wrap);
            h = wrap.count * TEXT_LINE_H;
            break;
        case MD_CODE_BLOCK:
            h = CODE_LINE_H;
            if (node_code_first[i]) h += CODE_PAD_V;
            if (node_code_last[i]) h += CODE_PAD_V;
            break;
        case MD_TABLE_ROW:
            h = TABLE_ROW_H;
            break;
        case MD_BLOCKQUOTE:
            word_wrap(n->text, n->text_len,
                      MDR_CONTENT_W - QUOTE_BAR_W - QUOTE_PAD, &wrap);
            h = wrap.count * TEXT_LINE_H + QUOTE_MARGIN;
            break;
        case MD_HRULE:
            h = HRULE_H;
            break;
        case MD_BLANK:
            h = BLANK_H;
            break;
        }

        node_height[i] = h;
        y += h;
    }

    return y + 8;
}

/* ======================================================================== */
/*  テキスト描画                                                            */
/* ======================================================================== */

extern int kcg_scale;

static int ci_match(char a, char b)
{
    if (a >= 'A' && a <= 'Z') a += 32;
    if (b >= 'A' && b <= 'Z') b += 32;
    return a == b;
}

static int node_contains(const MdNode *n, const char *term, int term_len)
{
    int i, j;
    int tlen;

    if (term_len == 0 || !n->text) return 0;
    tlen = n->text_len;
    if (tlen < term_len) return 0;

    for (i = 0; i <= tlen - term_len; i++) {
        int match = 1;
        for (j = 0; j < term_len; j++) {
            if (!ci_match(n->text[i + j], term[j])) {
                match = 0;
                break;
            }
        }
        if (match) return 1;
    }
    return 0;
}

static int draw_text_simple(int x, int y, const char *text, int len, u8 color)
{
    char buf[256];
    int n;

    n = (len < 255) ? len : 255;
    memcpy(buf, text, n);
    buf[n] = '\0';
    return kcg_draw_utf8(x, y, buf, color, 0xFF);
}

static int byte_to_px(const char *text, int byte_pos)
{
    const unsigned char *p = (const unsigned char *)text;
    int px = 0;
    int pos = 0;
    int scale = kcg_scale;

    while (pos < byte_pos && p[pos]) {
        if (p[pos] >= 0xE0) {
            px += 16 * scale;
            pos += 3;
        } else if (p[pos] >= 0xC0) {
            px += 8 * scale;
            pos += 2;
        } else {
            px += 8 * scale;
            pos += 1;
        }
    }
    return px;
}

static int draw_text_highlighted(int x, int y, const char *text, int len,
                                 u8 color, u8 hl_bg, int is_current)
{
    int i, j;
    int scale = kcg_scale;
    int font_h = 16 * scale;

    if (is_current) {
        gfx_fill_rect(x - 6, y, 3, font_h, MDR_COL_H1);
    }

    for (i = 0; i <= len - r_search.len; i++) {
        int match = 1;
        for (j = 0; j < r_search.len; j++) {
            if (!ci_match(text[i + j], r_search.term[j])) {
                match = 0;
                break;
            }
        }
        if (match) {
            int hl_x = x + byte_to_px(text, i);
            int hl_w = byte_to_px(text + i, r_search.len);
            gfx_fill_rect(hl_x, y, hl_w, font_h, hl_bg);
        }
    }

    return draw_text_simple(x, y, text, len, color);
}

static void draw_spans(int x, int y, const char *full_text,
                       const MdSpan *spans, int span_count, u8 base_color)
{
    int i;
    int cx = x;

    for (i = 0; i < span_count; i++) {
        u8 color;
        const char *seg_text;
        int seg_len;

        switch (spans[i].type) {
        case MD_SPAN_CODE: color = MDR_COL_CODE_FG; break;
        case MD_SPAN_BOLD: color = MDR_COL_BOLD; break;
        default:           color = base_color; break;
        }

        seg_text = full_text + spans[i].start;
        seg_len = spans[i].len;
        cx += draw_text_simple(cx, y, seg_text, seg_len, color);
    }
}

static int draw_wrapped_text(int x, int y,
                             const char *text, int text_len,
                             int max_width_px, u8 color,
                             const MdSpan *spans, int span_count)
{
    WrapResult wrap;
    int line;
    int cy = y;

    word_wrap(text, text_len, max_width_px, &wrap);

    for (line = 0; line < wrap.count; line++) {
        if (wrap.count == 1 && span_count > 0) {
            draw_spans(x, cy, text, spans, span_count, color);
        } else {
            draw_text_simple(x, cy, wrap.start[line], wrap.len[line], color);
        }
        cy += TEXT_LINE_H;
    }

    return cy - y;
}

/* ======================================================================== */
/*  ノード描画                                                              */
/* ======================================================================== */

static void render_node(MdDocument *doc, MdNode *n, int y_offset, int idx)
{
    int draw_y = node_y[idx] - y_offset;
    int draw_x;

    if (draw_y + node_height[idx] < 0) return;
    if (draw_y >= MDR_PAGE_H) return;

    switch (n->type) {
    case MD_H1:
        draw_y += H1_MARGIN_TOP;
        draw_x = MDR_MARGIN_LEFT + INDENT_H1;
        gfx_fill_rect(MDR_MARGIN_LEFT, draw_y, 4, FONT_H_S3, MDR_COL_H1_BAR);
        kcg_set_scale(3);
        draw_text_simple(draw_x, draw_y, n->text, n->text_len, MDR_COL_H1);
        kcg_set_scale(1);
        draw_y += FONT_H_S3 + 2;
        gfx_hline(MDR_MARGIN_LEFT, draw_y, MDR_CONTENT_W, MDR_COL_RULER);
        if (H1_BORDER_H > 1) {
            gfx_hline(MDR_MARGIN_LEFT, draw_y + 1, MDR_CONTENT_W, MDR_COL_RULER);
        }
        break;

    case MD_H2:
        draw_y += H2_MARGIN_TOP;
        draw_x = MDR_MARGIN_LEFT + INDENT_H2;
        kcg_set_scale(2);
        draw_text_simple(draw_x, draw_y, n->text, n->text_len, MDR_COL_H2);
        kcg_set_scale(1);
        draw_y += FONT_H_S2 + 2;
        gfx_hline(MDR_MARGIN_LEFT, draw_y, MDR_CONTENT_W, MDR_COL_RULER);
        break;

    case MD_H3:
        draw_y += H3_MARGIN_TOP;
        draw_x = MDR_MARGIN_LEFT + INDENT_H3;
        gfx_fill_rect(MDR_MARGIN_LEFT + INDENT_H3 - 6, draw_y + 2, 3, 12, MDR_COL_H3);
        draw_spans(draw_x, draw_y, n->text,
                   n->spans, n->span_count, MDR_COL_H3);
        break;

    case MD_PARAGRAPH:
        draw_x = MDR_MARGIN_LEFT;
        if (r_search.active && node_contains(n, r_search.term, r_search.len)) {
            int is_cur = (idx == r_search.current_idx);
            draw_text_highlighted(draw_x, draw_y, n->text, n->text_len,
                                  MDR_COL_TEXT, MDR_COL_SEARCH_BG, is_cur);
        } else {
            draw_wrapped_text(draw_x, draw_y, n->text, n->text_len,
                              MDR_CONTENT_W, MDR_COL_TEXT,
                              n->spans, n->span_count);
        }
        break;

    case MD_LIST_ITEM:
        draw_x = MDR_MARGIN_LEFT;
        draw_text_simple(draw_x, draw_y, ">", 1, MDR_COL_BULLET);
        if (r_search.active && node_contains(n, r_search.term, r_search.len)) {
            int is_cur = (idx == r_search.current_idx);
            draw_text_highlighted(draw_x + LIST_BULLET_W, draw_y,
                                  n->text, n->text_len,
                                  MDR_COL_TEXT, MDR_COL_SEARCH_BG, is_cur);
        } else {
            draw_wrapped_text(draw_x + LIST_BULLET_W, draw_y,
                              n->text, n->text_len,
                              MDR_CONTENT_W - LIST_BULLET_W, MDR_COL_TEXT,
                              n->spans, n->span_count);
        }
        break;

    case MD_CODE_BLOCK: {
        int bg_y = draw_y;
        int bg_h = CODE_LINE_H;
        int text_y = draw_y;

        if (node_code_first[idx]) {
            bg_y = draw_y;
            bg_h += CODE_PAD_V;
            text_y += CODE_PAD_V;
        }
        if (node_code_last[idx]) {
            bg_h += CODE_PAD_V;
        }

        gfx_fill_rect(MDR_MARGIN_LEFT, bg_y,
                       MDR_CONTENT_W, bg_h, MDR_COL_CODE_BG);
        draw_text_simple(MDR_MARGIN_LEFT + CODE_PAD_H, text_y,
                         n->text, n->text_len, MDR_COL_CODE_FG);
        break;
    }

    case MD_HRULE: {
        int hy = draw_y + HRULE_H / 2;
        int x;
        for (x = MDR_MARGIN_LEFT; x < MDR_MARGIN_LEFT + MDR_CONTENT_W; x += 6) {
            gfx_hline(x, hy, 3, MDR_COL_RULER);
        }
        break;
    }

    case MD_TABLE_ROW: {
        int col;
        int col_w;
        int cx;
        int is_header;

        is_header = (idx == 0 ||
                     doc->nodes[idx - 1].type != MD_TABLE_ROW);

        if (!is_header) {
            int row_idx = 0;
            int k;
            for (k = idx - 1; k >= 0 && doc->nodes[k].type == MD_TABLE_ROW; k--) {
                row_idx++;
            }
            if (row_idx % 2 == 0) {
                gfx_fill_rect(MDR_MARGIN_LEFT, draw_y,
                              MDR_CONTENT_W, TABLE_ROW_H, MDR_COL_QUOTE_BAR);
            }
        } else {
            gfx_fill_rect(MDR_MARGIN_LEFT, draw_y,
                          MDR_CONTENT_W, TABLE_ROW_H, MDR_COL_CODE_BG);
        }

        if (n->col_count > 0) {
            col_w = MDR_CONTENT_W / n->col_count;
        } else {
            col_w = MDR_CONTENT_W;
        }

        cx = MDR_MARGIN_LEFT;
        for (col = 0; col < n->col_count; col++) {
            u8 fg = is_header ? MDR_COL_H2 : MDR_COL_TEXT;
            draw_text_simple(cx + TABLE_PAD, draw_y + 2,
                             n->cols[col], n->col_lens[col], fg);
            cx += col_w;
        }

        cx = MDR_MARGIN_LEFT;
        for (col = 1; col < n->col_count; col++) {
            cx += col_w;
            gfx_vline(cx, draw_y, TABLE_ROW_H, MDR_COL_RULER);
        }

        gfx_hline(MDR_MARGIN_LEFT, draw_y + TABLE_ROW_H - 1,
                  MDR_CONTENT_W, MDR_COL_RULER);
        break;
    }

    case MD_BLOCKQUOTE: {
        int bq_x = MDR_MARGIN_LEFT + QUOTE_BAR_W + QUOTE_PAD;
        int bq_w = MDR_CONTENT_W - QUOTE_BAR_W - QUOTE_PAD;

        gfx_fill_rect(MDR_MARGIN_LEFT, draw_y,
                      QUOTE_BAR_W, node_height[idx], MDR_COL_QUOTE_BAR);

        draw_wrapped_text(bq_x, draw_y + 2,
                          n->text, n->text_len,
                          bq_w, MDR_COL_TEXT,
                          n->spans, n->span_count);
        break;
    }

    case MD_BLANK:
        break;
    }
}

/* ======================================================================== */
/*  ページ描画 (スクロール差分最適化)                                        */
/* ======================================================================== */

static int prev_scroll_y = -9999;

static void clear_strip(int y, int h)
{
    if (y < 0) { h += y; y = 0; }
    if (y + h > MDR_PAGE_H) h = MDR_PAGE_H - y;
    if (h > 0) gfx_fill_rect(0, y, MDR_SCREEN_W, h, MDR_COL_BG);
}

static void shift_backbuffer(int delta)
{
    int p;
    int pitch = gfx_fb.pitch;
    int abs_delta = (delta > 0) ? delta : -delta;
    int move_lines = MDR_PAGE_H - abs_delta;

    if (move_lines <= 0) return;

    for (p = 0; p < 4; p++) {
        u8 *plane = gfx_fb.planes[p];
        if (delta > 0) {
            memmove(plane,
                    plane + delta * pitch,
                    move_lines * pitch);
        } else {
            memmove(plane + abs_delta * pitch,
                    plane,
                    move_lines * pitch);
        }
    }
}

static void render_strip_nodes(MdDocument *doc, int scroll_y,
                               int strip_top, int strip_bottom)
{
    int i;
    for (i = 0; i < doc->node_count; i++) {
        int ny = node_y[i] - scroll_y;
        int nh = node_height[i];

        if (ny >= strip_bottom) break;
        if (ny + nh < strip_top) continue;

        render_node(doc, &doc->nodes[i], scroll_y, i);
    }
}

void md_render_page(MdDocument *doc, int scroll_y)
{
    int delta = scroll_y - prev_scroll_y;
    int abs_delta = (delta > 0) ? delta : -delta;

    if (abs_delta >= MDR_PAGE_H || prev_scroll_y < 0) {
        gfx_fill_rect(0, 0, MDR_SCREEN_W, MDR_PAGE_H, MDR_COL_BG);
        {
            int i;
            for (i = 0; i < doc->node_count; i++) {
                int ny = node_y[i] - scroll_y;
                int nh = node_height[i];

                if (ny >= MDR_PAGE_H) break;
                if (ny + nh < 0) continue;

                render_node(doc, &doc->nodes[i], scroll_y, i);
            }
        }
    } else if (delta > 0) {
        shift_backbuffer(delta);
        clear_strip(MDR_PAGE_H - delta, delta);
        render_strip_nodes(doc, scroll_y, MDR_PAGE_H - delta, MDR_PAGE_H);
    } else if (delta < 0) {
        shift_backbuffer(delta);
        clear_strip(0, abs_delta);
        render_strip_nodes(doc, scroll_y, 0, abs_delta);
    }

    prev_scroll_y = scroll_y;
}

/* ======================================================================== */
/*  ステータスバー                                                          */
/* ======================================================================== */

void md_render_statusbar(const char *filename, int scroll_y, int total_h)
{
    int pct;
    char buf[80];
    int len;

    gfx_fill_rect(0, MDR_PAGE_H, MDR_SCREEN_W, MDR_STATUS_H, MDR_COL_STATUS_BG);

    len = 0;
    buf[len++] = ' ';
    {
        const char *p = filename;
        while (*p && len < 40) buf[len++] = *p++;
    }
    buf[len] = '\0';
    kcg_draw_utf8(4, MDR_PAGE_H + 2, buf, MDR_COL_STATUS_FG, 0xFF);

    if (total_h > MDR_PAGE_H) {
        pct = (scroll_y * 100) / (total_h - MDR_PAGE_H);
        if (pct > 100) pct = 100;
    } else {
        pct = 100;
    }
    sprintf(buf, "%d%%", pct);
    kcg_draw_utf8(MDR_SCREEN_W - 60, MDR_PAGE_H + 2, buf, MDR_COL_STATUS_FG, 0xFF);

    kcg_draw_utf8(MDR_SCREEN_W / 2 - 120, MDR_PAGE_H + 2,
                  "Spc:Pg b:Back /:Find t:TOC F1:Open q:Quit",
                  MDR_COL_STATUS_FG, 0xFF);
}

/* ======================================================================== */
/*  API                                                                      */
/* ======================================================================== */

void md_render_init(KernelAPI *api)
{
    r_api = api;
    memset(&r_search, 0, sizeof(r_search));
    prev_scroll_y = -9999;
}

void md_render_reset_scroll(void)
{
    prev_scroll_y = -9999;
}

int md_render_get_node_y(int node_idx)
{
    return node_y[node_idx];
}

void md_render_set_search(const MdSearchState *state)
{
    memcpy(&r_search, state, sizeof(MdSearchState));
}

const MdSearchState *md_render_get_search(void)
{
    return &r_search;
}
