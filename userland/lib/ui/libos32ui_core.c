/* ======================================================================== */
/*  LIBOS32UI_CORE.C — microUI OS32統合 (レンダラ + 入力ブリッジ)           */
/*                                                                          */
/*  GFXバックバッファへの描画と、KAPI経由の入力注入を行う。                  */
/* ======================================================================== */

#include <string.h>
#include "libos32ui.h"
#include "libos32gfx.h"
#include "gfx_font.h"

/* ======================================================================== */
/*  内部状態                                                                */
/* ======================================================================== */

static KernelAPI *ui_api;
static int ui_font_w = 8;
static int ui_font_h = 8;

/* 前フレームのマウスボタン状態 (エッジ検出用) */
static unsigned char prev_buttons;

/* ======================================================================== */
/*  テキスト幅・高さコールバック (microui が呼ぶ)                           */
/* ======================================================================== */

static int cb_text_width(mu_Font font, const char *str, int len)
{
    int n;
    (void)font;
    if (len < 0) {
        /* strlen 相当 */
        const char *p = str;
        while (*p) p++;
        n = (int)(p - str);
    } else {
        n = len;
    }
    return n * ui_font_w;
}

static int cb_text_height(mu_Font font)
{
    (void)font;
    return ui_font_h;
}

/* ======================================================================== */
/*  クリッピング矩形 (ソフトウェアクリップ)                                */
/* ======================================================================== */

static mu_Rect clip_rect = { 0, 0, 640, 400 };

static void set_clip(const mu_Rect *r)
{
    clip_rect = *r;
}

/* クリップ済み矩形塗りを行う */
static void fill_rect_clipped(int x, int y, int w, int h, unsigned char color)
{
    int x2, y2, cx2, cy2;

    /* クリップ適用 */
    x2 = x + w;
    y2 = y + h;
    cx2 = clip_rect.x + clip_rect.w;
    cy2 = clip_rect.y + clip_rect.h;

    if (x < clip_rect.x) x = clip_rect.x;
    if (y < clip_rect.y) y = clip_rect.y;
    if (x2 > cx2) x2 = cx2;
    if (y2 > cy2) y2 = cy2;

    w = x2 - x;
    h = y2 - y;
    if (w <= 0 || h <= 0) return;

    gfx_fill_rect(x, y, w, h, color);
}

/* クリップ済み文字列描画 */
static void draw_text_clipped(int x, int y, const char *str, unsigned char color)
{
    /* クリップ範囲外なら何もしない */
    if (y + ui_font_h <= clip_rect.y) return;
    if (y >= clip_rect.y + clip_rect.h) return;

    /* 文字単位でクリップ (簡易実装) */
    while (*str) {
        if (x + ui_font_w > clip_rect.x && x < clip_rect.x + clip_rect.w) {
            gfx_putchar(x, y, *str, color);
        }
        x += ui_font_w;
        str++;
        if (x >= clip_rect.x + clip_rect.w) break;
    }
}

/* ======================================================================== */
/*  アイコン描画 (簡易ビットマップ)                                        */
/* ======================================================================== */

static void draw_icon(int id, mu_Rect r, unsigned char color)
{
    int cx = r.x + (r.w - 8) / 2;
    int cy = r.y + (r.h - 8) / 2;

    switch (id) {
    case MU_ICON_CLOSE:
        /* × マーク */
        gfx_line(cx + 1, cy + 1, cx + 6, cy + 6, color);
        gfx_line(cx + 6, cy + 1, cx + 1, cy + 6, color);
        break;
    case MU_ICON_CHECK:
        /* チェックマーク */
        gfx_line(cx + 1, cy + 4, cx + 3, cy + 6, color);
        gfx_line(cx + 3, cy + 6, cx + 7, cy + 1, color);
        break;
    case MU_ICON_COLLAPSED:
        /* ▶ (右向き三角) */
        gfx_line(cx + 2, cy + 1, cx + 2, cy + 7, color);
        gfx_line(cx + 2, cy + 1, cx + 6, cy + 4, color);
        gfx_line(cx + 2, cy + 7, cx + 6, cy + 4, color);
        break;
    case MU_ICON_EXPANDED:
        /* ▼ (下向き三角) */
        gfx_line(cx + 1, cy + 2, cx + 7, cy + 2, color);
        gfx_line(cx + 1, cy + 2, cx + 4, cy + 6, color);
        gfx_line(cx + 7, cy + 2, cx + 4, cy + 6, color);
        break;
    }
}

/* ======================================================================== */
/*  初期化                                                                  */
/* ======================================================================== */

void mui_init(mu_Context *ctx, KernelAPI *api, int font_w, int font_h)
{
    ui_api = api;
    ui_font_w = font_w;
    ui_font_h = font_h;
    prev_buttons = 0;

    mu_init(ctx);
    ctx->text_width = cb_text_width;
    ctx->text_height = cb_text_height;

    /* デフォルトスタイルをPC-98 16色パレットに設定 */
    /* mu_Color.r にパレットインデックスを格納する方式 */
    ctx->style->colors[MU_COLOR_TEXT]        = mu_color(15, 0, 0, 255); /* 白 */
    ctx->style->colors[MU_COLOR_BORDER]      = mu_color( 8, 0, 0, 255); /* 暗灰 */
    ctx->style->colors[MU_COLOR_WINDOWBG]    = mu_color( 1, 0, 0, 255); /* 青 */
    ctx->style->colors[MU_COLOR_TITLEBG]     = mu_color( 4, 0, 0, 255); /* 暗赤 */
    ctx->style->colors[MU_COLOR_TITLETEXT]   = mu_color(15, 0, 0, 255); /* 白 */
    ctx->style->colors[MU_COLOR_PANELBG]     = mu_color( 0, 0, 0,   0); /* 透明 */
    ctx->style->colors[MU_COLOR_BUTTON]      = mu_color( 7, 0, 0, 255); /* 灰 */
    ctx->style->colors[MU_COLOR_BUTTONHOVER] = mu_color( 9, 0, 0, 255); /* 明青 */
    ctx->style->colors[MU_COLOR_BUTTONFOCUS] = mu_color(11, 0, 0, 255); /* 明シアン */
    ctx->style->colors[MU_COLOR_BASE]        = mu_color( 0, 0, 0, 255); /* 黒 */
    ctx->style->colors[MU_COLOR_BASEHOVER]   = mu_color( 8, 0, 0, 255); /* 暗灰 */
    ctx->style->colors[MU_COLOR_BASEFOCUS]   = mu_color( 7, 0, 0, 255); /* 灰 */
    ctx->style->colors[MU_COLOR_SCROLLBASE]  = mu_color( 8, 0, 0, 255); /* 暗灰 */
    ctx->style->colors[MU_COLOR_SCROLLTHUMB] = mu_color( 7, 0, 0, 255); /* 灰 */
}

/* ======================================================================== */
/*  入力ブリッジ                                                            */
/* ======================================================================== */

/* キー1文字を microUI へ流し込む。
   ch <= 0 なら何もしない (キーなし) */
static void feed_char(mu_Context *ctx, int ch)
{
    if (ch <= 0) return;

    if (ch == 0x08) {
        mu_input_keydown(ctx, MU_KEY_BACKSPACE);
        mu_input_keyup(ctx, MU_KEY_BACKSPACE);
    } else if (ch == 0x0D || ch == 0x0A) {
        mu_input_keydown(ctx, MU_KEY_RETURN);
        mu_input_keyup(ctx, MU_KEY_RETURN);
    } else if (ch >= 0x20 && ch < 0x7F) {
        char buf[2];
        buf[0] = (char)ch;
        buf[1] = '\0';
        mu_input_text(ctx, buf);
    }
}

void mui_pump_input_ch(mu_Context *ctx, int ch)
{
    MouseInfo mi;
    unsigned char btn;

    ui_api->mouse_poll(&mi);
    mu_input_mousemove(ctx, mi.x, mi.y);

    btn = mi.buttons;

    /* 左ボタン */
    if ((btn & MOUSE_BTN_LEFT) && !(prev_buttons & MOUSE_BTN_LEFT))
        mu_input_mousedown(ctx, mi.x, mi.y, MU_MOUSE_LEFT);
    if (!(btn & MOUSE_BTN_LEFT) && (prev_buttons & MOUSE_BTN_LEFT))
        mu_input_mouseup(ctx, mi.x, mi.y, MU_MOUSE_LEFT);

    /* 右ボタン */
    if ((btn & MOUSE_BTN_RIGHT) && !(prev_buttons & MOUSE_BTN_RIGHT))
        mu_input_mousedown(ctx, mi.x, mi.y, MU_MOUSE_RIGHT);
    if (!(btn & MOUSE_BTN_RIGHT) && (prev_buttons & MOUSE_BTN_RIGHT))
        mu_input_mouseup(ctx, mi.x, mi.y, MU_MOUSE_RIGHT);

    prev_buttons = btn;

    /* キーボード: Backspace / Enter のエッジ検出 */
    feed_char(ctx, ch);
}

void mui_pump_input(mu_Context *ctx)
{
    /* キーはここで読み切ってしまうので、アプリ側でも自前でキーを処理したい
       場合は mui_pump_input_ch() に読んだ文字を渡すこと。
       (この関数を使うと kbd_trygetchar() の戻りが常に空になる) */
    mui_pump_input_ch(ctx, ui_api->kbd_trygetchar());
}

/* ======================================================================== */
/*  レンダラ                                                                */
/* ======================================================================== */

int mui_render_cached(mu_Context *ctx, int force)
{
    /* mu のコマンドリストはフレームごとに先頭から組み直されるので、
       バイト列が一致すれば見た目も同一。まるごと比較して同じなら
       描画も VRAM 転送も発生させない */
    static char prev[MU_COMMANDLIST_SIZE];
    static int prev_len = -1;
    int len = ctx->command_list.idx;

    if (!force && len == prev_len &&
        memcmp(ctx->command_list.items, prev, (size_t)len) == 0) {
        return 0;
    }
    memcpy(prev, ctx->command_list.items, (size_t)len);
    prev_len = len;
    mui_render(ctx);
    return 1;
}

void mui_render(mu_Context *ctx)
{
    mu_Command *cmd = NULL;

    while (mu_next_command(ctx, &cmd)) {
        switch (cmd->type) {
        case MU_COMMAND_TEXT:
            draw_text_clipped(
                cmd->text.pos.x, cmd->text.pos.y,
                cmd->text.str, cmd->text.color.r);
            break;
        case MU_COMMAND_RECT:
            fill_rect_clipped(
                cmd->rect.rect.x, cmd->rect.rect.y,
                cmd->rect.rect.w, cmd->rect.rect.h,
                cmd->rect.color.r);
            break;
        case MU_COMMAND_ICON:
            draw_icon(cmd->icon.id, cmd->icon.rect, cmd->icon.color.r);
            break;
        case MU_COMMAND_CLIP:
            set_clip(&cmd->clip.rect);
            break;
        }
    }
}

/* ======================================================================== */
/*  パレット設定                                                            */
/* ======================================================================== */

void mui_set_color(mu_Context *ctx, int colorid, unsigned char pal_idx)
{
    if (colorid >= 0 && colorid < MU_COLOR_MAX) {
        ctx->style->colors[colorid].r = pal_idx;
    }
}
