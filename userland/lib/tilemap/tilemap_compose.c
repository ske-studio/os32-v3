#include <string.h>
#include "tilemap_internal.h"

/* ======================================================================== */
/*  ASMルーチン宣言                                                         */
/* ======================================================================== */

extern void __cdecl asm_tile_pair_opaque(
    u8 **bb_planes, int dst_off, int bb_pitch,
    const u8 *tile_l, const u8 *tile_r, int tile_psz);

extern void __cdecl asm_tile_pair_masked(
    u8 **bb_planes, int dst_off, int bb_pitch,
    const u8 *tile_l, const u8 *tile_r, int tile_psz,
    u8 *cov_pair);

extern const u8 _asm_byte_reverse_lut[256];

extern void __cdecl asm_bb_shift_horiz(
    u8 **planes, int pitch, int origin_y, int tile_h,
    int base_byte, int tile_bytes, int shift_bytes);

extern void __cdecl asm_bb_shift_vert(
    u8 **planes, int pitch, int origin_y, int tile_h,
    int base_byte, int tile_bytes, int shift_lines);

extern void __cdecl asm_draw_column_tiles(
    u8 **bb_planes, int bb_pitch, int dst_byte_x, int dst_y_start,
    const u8 **tile_ptrs, int tile_psz, int tile_count);

extern void __cdecl asm_draw_row_tiles(
    u8 **bb_planes, int bb_pitch, int dst_byte_x_start, int dst_y,
    const u8 **tile_ptrs, int tile_psz, int tile_count);

/* ======================================================================== */
/*  静的バッファ (BSS — heap確保を排除)                                     */
/* ======================================================================== */

/* カバレッジマスク: 2タイルペア単位 (DWORD/行)
 * 既定構成 (26x20) で 13ペア × 20行 × (4bytes × 16行) = 16,640 bytes */
#define COV_PAIRS       (TILEMAP_COLS / 2)  /* 既定 13 */
#define COV_PAIR_BYTES  (4 * TILE_H)        /* 64 */

/* スクロール差分の大ジャンプ閾値 (プレーン幅/高さの半分) */
#define SCROLL_JUMP_THRESHOLD_X  ((TILEMAP_COLS * TILE_W) / 2)  /* 既定 208px */
#define SCROLL_JUMP_THRESHOLD_Y  ((TILEMAP_ROWS * TILE_H) / 2)  /* 既定 160px */

static u8 s_cov_mask[TILEMAP_ROWS][COV_PAIRS][COV_PAIR_BYTES];
static u8 s_cov_full[TILEMAP_ROWS][COV_PAIRS];
static u8 s_cell_dirty[TILEMAP_ROWS][TILEMAP_COLS];

/* 描画済み列/行の追跡 (列単位ダーティレクト用) */
static u8 s_col_drawn[TILEMAP_COLS];
static int s_drawn_row_min;
static int s_drawn_row_max;

static void drawn_tracking_reset(void)
{
    memset(s_col_drawn, 0, sizeof(s_col_drawn));
    s_drawn_row_min = TILEMAP_ROWS;
    s_drawn_row_max = -1;
}

static void drawn_tracking_mark(int col, int row)
{
    s_col_drawn[col] = 1;
    if (row < s_drawn_row_min) s_drawn_row_min = row;
    if (row > s_drawn_row_max) s_drawn_row_max = row;
}

/* 表示クリップ矩形 (tilemap_set_clip で設定。w<=0 = 無効) */
static int s_clip_x = 0, s_clip_y = 0, s_clip_w = 0, s_clip_h = 0;

/* 追跡結果から最小限の dirty rect を登録 (クリップ矩形内に収める) */
static void flush_drawn_dirty(void)
{
    int c, start;
    int dy, dh;

    if (s_drawn_row_max < 0) return;

    dy = _tilemap.origin_y + s_drawn_row_min * TILE_H;
    dh = (s_drawn_row_max - s_drawn_row_min + 1) * TILE_H;
    if (s_clip_w > 0 && s_clip_h > 0) {
        if (dy < s_clip_y) { dh -= s_clip_y - dy; dy = s_clip_y; }
        if (dy + dh > s_clip_y + s_clip_h) dh = s_clip_y + s_clip_h - dy;
        if (dh <= 0) return;
    }

    c = 0;
    while (c < TILEMAP_COLS) {
        int rx, rw;
        if (!s_col_drawn[c]) { c++; continue; }
        start = c;
        while (c < TILEMAP_COLS && s_col_drawn[c]) c++;
        rx = _tilemap.origin_x + start * TILE_W;
        rw = (c - start) * TILE_W;
        if (s_clip_w > 0 && s_clip_h > 0) {
            if (rx < s_clip_x) { rw -= s_clip_x - rx; rx = s_clip_x; }
            if (rx + rw > s_clip_x + s_clip_w) rw = s_clip_x + s_clip_w - rx;
            if (rw <= 0) continue;
        }
        _tilemap.kapi->gfx_add_dirty_rect(rx, dy, rw, dh);
    }
}

/* ======================================================================== */
/*  ユーティリティ                                                          */
/* ======================================================================== */

/* TileDef から一時的な GFX_Surface を作成 */
static void tile_to_surface(GFX_Surface *s, const TileDef *t)
{
    s->w = TILE_W;
    s->h = TILE_H;
    s->pitch = TILE_PITCH;
    s->planes[0] = (u8 *)t->planes[0];
    s->planes[1] = (u8 *)t->planes[1];
    s->planes[2] = (u8 *)t->planes[2];
    s->planes[3] = (u8 *)t->planes[3];
    s->_pool_idx = -1;
}

/* H/Vフリップ済みタイルデータを一時バッファに生成 (LUT使用) */
static void tile_to_surface_flipped(GFX_Surface *s, const TileDef *t,
                                    int hflip, int vflip,
                                    u8 flip_buf[4][TILE_PLANE_SZ])
{
    int p, row;
    for (p = 0; p < 4; p++) {
        for (row = 0; row < TILE_H; row++) {
            int src_row = vflip ? (TILE_H - 1 - row) : row;
            u8 b0 = t->planes[p][src_row * TILE_PITCH];
            u8 b1 = t->planes[p][src_row * TILE_PITCH + 1];

            if (hflip) {
                flip_buf[p][row * TILE_PITCH]     = _asm_byte_reverse_lut[b1];
                flip_buf[p][row * TILE_PITCH + 1] = _asm_byte_reverse_lut[b0];
            } else {
                flip_buf[p][row * TILE_PITCH]     = b0;
                flip_buf[p][row * TILE_PITCH + 1] = b1;
            }
        }
        s->planes[p] = flip_buf[p];
    }
    s->w = TILE_W;
    s->h = TILE_H;
    s->pitch = TILE_PITCH;
    s->_pool_idx = -1;
}

/* 表示クリップ矩形を設定する (画面座標)。w<=0 なら無効 = 従来どおり全域。
   スクロールの糊しろタイルが盤面の外 (UIパネル等) を上書きしないように
   使う。tilemap_compose_btf / ftb 系に効く。 */
void tilemap_set_clip(int x, int y, int w, int h)
{
    s_clip_x = x;
    s_clip_y = y;
    s_clip_w = w;
    s_clip_h = h;
}

/* スクロールを考慮したタイル描画座標とsrc_rectを計算 */
static int calc_tile_draw(int col, int row,
                          i16 scroll_x, i16 scroll_y,
                          int origin_x, int origin_y,
                          int *out_dx, int *out_dy,
                          GFX_Rect *out_sr, int *use_sr)
{
    int map_x = col * TILE_W - scroll_x;
    int map_y = row * TILE_H - scroll_y;
    int dx, dy, sx, sy, sw, sh;
    int bx0, by0, bx1, by1;

    while (map_x < -TILE_W) map_x += TILEMAP_COLS * TILE_W;
    while (map_x >= TILEMAP_COLS * TILE_W) map_x -= TILEMAP_COLS * TILE_W;
    while (map_y < -TILE_H) map_y += TILEMAP_ROWS * TILE_H;
    while (map_y >= TILEMAP_ROWS * TILE_H) map_y -= TILEMAP_ROWS * TILE_H;

    dx = origin_x + map_x;
    dy = origin_y + map_y;
    sx = 0; sy = 0; sw = TILE_W; sh = TILE_H;

    /* 描画可能な範囲 = プレーン全域 ∩ クリップ矩形 */
    bx0 = origin_x;
    by0 = origin_y;
    bx1 = origin_x + TILEMAP_COLS * TILE_W;
    by1 = origin_y + TILEMAP_ROWS * TILE_H;
    if (s_clip_w > 0 && s_clip_h > 0) {
        if (s_clip_x > bx0) bx0 = s_clip_x;
        if (s_clip_y > by0) by0 = s_clip_y;
        if (s_clip_x + s_clip_w < bx1) bx1 = s_clip_x + s_clip_w;
        if (s_clip_y + s_clip_h < by1) by1 = s_clip_y + s_clip_h;
    }

    if (dx < bx0) {
        int clip = bx0 - dx;
        sx += clip; sw -= clip; dx = bx0;
    }
    if (dy < by0) {
        int clip = by0 - dy;
        sy += clip; sh -= clip; dy = by0;
    }
    if (dx + sw > bx1) {
        sw = bx1 - dx;
    }
    if (dy + sh > by1) {
        sh = by1 - dy;
    }

    if (sw <= 0 || sh <= 0) return 0;

    *out_dx = dx;
    *out_dy = dy;
    if (sx != 0 || sy != 0 || sw != TILE_W || sh != TILE_H) {
        out_sr->x = sx;
        out_sr->y = sy;
        out_sr->w = sw;
        out_sr->h = sh;
        *use_sr = 1;
    } else {
        *use_sr = 0;
    }
    return 1;
}

/* タイルの GFX_Surface を準備する (反転を含む) */
static void prepare_tile_surface(GFX_Surface *surf, const TileDef *tile,
                                 u16 attr,
                                 u8 flip_buf[4][TILE_PLANE_SZ])
{
    int hflip = TILEMAP_HFLIP(attr);
    int vflip = TILEMAP_VFLIP(attr);

    if (hflip || vflip) {
        tile_to_surface_flipped(surf, tile, hflip, vflip, flip_buf);
    } else {
        tile_to_surface(surf, tile);
    }
}

/* カバレッジペア (64 bytes) が完全カバー済みか DWORD 判定 */
static int check_cov_full_64(const u8 *cov)
{
    const u32 *p = (const u32 *)cov;
    int i;
    for (i = 0; i < COV_PAIR_BYTES / 4; i++) {
        if (p[i] != 0xFFFFFFFF) return 0;
    }
    return 1;
}

/* カバレッジペア (64 bytes) が空か DWORD 判定 */
static int check_cov_empty_64(const u8 *cov)
{
    const u32 *p = (const u32 *)cov;
    int i;
    for (i = 0; i < COV_PAIR_BYTES / 4; i++) {
        if (p[i] != 0) return 0;
    }
    return 1;
}

/* ダーティセルの検出とクリア (共通) */
static void detect_dirty(void)
{
    int row, col, bg;
    for (row = 0; row < TILEMAP_ROWS; row++) {
        for (col = 0; col < TILEMAP_COLS; col++) {
            s_cell_dirty[row][col] = 0;
            for (bg = 0; bg < BG_COUNT; bg++) {
                if (_tilemap.bg_planes[bg].dirty[row][col]) {
                    s_cell_dirty[row][col] = 1;
                    _tilemap.bg_planes[bg].dirty[row][col] = 0;
                }
            }
        }
    }
}

/* ======================================================================== */
/*  共通タイル描画ヘルパー                                                  */
/* ======================================================================== */

/*
 * 1タイルを BtF 方式で全BG分描画する共通ヘルパー。
 *
 * screen_col, screen_row: BB上の描画位置 (座標計算 + drawn_tracking に使用)
 * map_col, map_row:       タイルマップ上のインデックス (map[][] の参照に使用)
 * bg_start:               走査開始BG (0=全BG、1=上層BGのみ)
 * use_scroll:             1=calc_tile_draw使用 (スクロール+クリッピング対応)
 */
static void draw_one_tile_btf(int screen_col, int screen_row,
                               int map_col, int map_row,
                               int bg_start, int use_scroll)
{
    int bg;
    GFX_Surface tmp_surf;
    u8 flip_buf[4][TILE_PLANE_SZ];

    for (bg = bg_start; bg < BG_COUNT; bg++) {
        int tile_id;
        u16 attr;
        TileDef *tile;
        int dx, dy;

        if (!_tilemap.bg_planes[bg].visible) continue;

        attr = _tilemap.bg_planes[bg].map[map_row][map_col];
        tile_id = TILEMAP_ID(attr);
        tile = &_tilemap.tiles[tile_id];

        if (tile->opacity == TILE_TRANSPARENT) continue;

        if (use_scroll) {
            int use_sr;
            GFX_Rect sr;
            if (!calc_tile_draw(map_col, map_row,
                                _tilemap.bg_planes[bg].scroll_x,
                                _tilemap.bg_planes[bg].scroll_y,
                                _tilemap.origin_x, _tilemap.origin_y,
                                &dx, &dy, &sr, &use_sr))
                continue;
            prepare_tile_surface(&tmp_surf, tile, attr, flip_buf);
            if (tile->opacity == TILE_OPAQUE) {
                gfx_blit(dx, dy, &tmp_surf, use_sr ? &sr : NULL);
            } else {
                gfx_blit_transparent(dx, dy, &tmp_surf, use_sr ? &sr : NULL);
            }
        } else {
            dx = _tilemap.origin_x + screen_col * TILE_W;
            dy = _tilemap.origin_y + screen_row * TILE_H;
            prepare_tile_surface(&tmp_surf, tile, attr, flip_buf);
            if (tile->opacity == TILE_OPAQUE) {
                gfx_blit(dx, dy, &tmp_surf, NULL);
            } else {
                gfx_blit_transparent(dx, dy, &tmp_surf, NULL);
            }
        }
        drawn_tracking_mark(screen_col, screen_row);
    }
}

/*
 * BG0 の指定列または行が NASM 高速パス条件を満たすか判定。
 * axis: 0=列方向 (map[i][fixed_idx] を走査), 1=行方向 (map[fixed_idx][i] を走査)
 * fixed_idx: 固定される列 or 行インデックス
 * count: 走査する要素数
 */
static int check_can_fast(int axis, int fixed_idx, int count)
{
    int i;
    if (!_tilemap.bg_planes[0].visible) return 0;
    for (i = 0; i < count; i++) {
        u16 a;
        TileDef *t;
        if (axis == 0)
            a = _tilemap.bg_planes[0].map[i][fixed_idx];
        else
            a = _tilemap.bg_planes[0].map[fixed_idx][i];
        t = &_tilemap.tiles[TILEMAP_ID(a)];
        if (t->opacity != TILE_OPAQUE || TILEMAP_HFLIP(a) ||
            TILEMAP_VFLIP(a))
            return 0;
    }
    return 1;
}

/* ======================================================================== */
/*  Back-to-Front 合成                                                      */
/* ======================================================================== */

void tilemap_compose_btf(void)
{
    int row, col;

    if (!_tilemap.kapi || !_tilemap.bg_planes) return;

    detect_dirty();
    drawn_tracking_reset();

    gfx_dirty_suppress = 1;

    for (row = 0; row < TILEMAP_ROWS; row++) {
        for (col = 0; col < TILEMAP_COLS; col++) {
            if (!s_cell_dirty[row][col]) continue;
            draw_one_tile_btf(col, row, col, row, 0, 1);
        }
    }

    gfx_dirty_suppress = 0;

    /* 描画された列/行範囲のみ dirty rect を登録 */
    flush_drawn_dirty();
}

/* ======================================================================== */
/*  Front-to-Back 合成 (DWORD ペア転送 + BSS カバレッジ)                    */
/* ======================================================================== */

void tilemap_compose_ftb(void)
{
    int row, col, bg;

    if (!_tilemap.kapi || !_tilemap.bg_planes) return;

    detect_dirty();
    drawn_tracking_reset();

    memset(s_cov_mask, 0, sizeof(s_cov_mask));
    memset(s_cov_full, 0, sizeof(s_cov_full));

    /* BG3(最前面) → BG0(最背面) */
    for (bg = BG_COUNT - 1; bg >= 0; bg--) {
        if (!_tilemap.bg_planes[bg].visible) continue;

        for (row = 0; row < TILEMAP_ROWS; row++) {
            for (col = 0; col < TILEMAP_COLS; col += 2) {
                int pair = col >> 1;
                int tile_id_l, tile_id_r;
                TileDef *tile_l, *tile_r;
                int dx, dy, dst_off;
                u8 *cov_pair;

                /* ペア内どちらかがdirtyなら処理 */
                if (!s_cell_dirty[row][col] &&
                    !s_cell_dirty[row][col + 1]) continue;
                if (s_cov_full[row][pair]) continue;

                tile_id_l = TILEMAP_ID(
                    _tilemap.bg_planes[bg].map[row][col]);
                tile_id_r = TILEMAP_ID(
                    _tilemap.bg_planes[bg].map[row][col + 1]);
                tile_l = &_tilemap.tiles[tile_id_l];
                tile_r = &_tilemap.tiles[tile_id_r];

                /* 両方透明 → スキップ */
                if (tile_l->opacity == TILE_TRANSPARENT &&
                    tile_r->opacity == TILE_TRANSPARENT) continue;

                dx = _tilemap.origin_x + col * TILE_W;
                dy = _tilemap.origin_y + row * TILE_H;
                dst_off = dy * gfx_fb.pitch + (dx >> 3);
                cov_pair = s_cov_mask[row][pair];

                if (tile_l->opacity == TILE_OPAQUE &&
                    tile_r->opacity == TILE_OPAQUE &&
                    check_cov_empty_64(cov_pair)) {
                    asm_tile_pair_opaque(
                        gfx_fb.planes, dst_off, gfx_fb.pitch,
                        tile_l->planes[0], tile_r->planes[0],
                        TILE_PLANE_SZ);
                    s_cov_full[row][pair] = 1;
                    memset(cov_pair, 0xFF, COV_PAIR_BYTES);
                } else {
                    asm_tile_pair_masked(
                        gfx_fb.planes, dst_off, gfx_fb.pitch,
                        tile_l->planes[0], tile_r->planes[0],
                        TILE_PLANE_SZ, cov_pair);
                    if (check_cov_full_64(cov_pair))
                        s_cov_full[row][pair] = 1;
                }
                drawn_tracking_mark(col, row);
                drawn_tracking_mark(col + 1, row);
            }
        }
    }

    /* 描画された列/行範囲のみ dirty rect を登録 */
    flush_drawn_dirty();
}

/* ======================================================================== */
/*  2タイル同時 32bit 転送 (ASM使用)                                        */
/* ======================================================================== */

void tilemap_compose_btf_fast(void)
{
    int row, col, bg;

    if (!_tilemap.kapi || !_tilemap.bg_planes) return;

    detect_dirty();
    drawn_tracking_reset();

    for (bg = 0; bg < BG_COUNT; bg++) {
        if (!_tilemap.bg_planes[bg].visible) continue;

        for (row = 0; row < TILEMAP_ROWS; row++) {
            for (col = 0; col < TILEMAP_COLS; col += 2) {
                int dx, dy;
                int tile_id_l, tile_id_r;
                TileDef *tile_l, *tile_r;

                if (!s_cell_dirty[row][col] &&
                    !s_cell_dirty[row][col + 1]) continue;

                tile_id_l = TILEMAP_ID(
                    _tilemap.bg_planes[bg].map[row][col]);
                tile_id_r = TILEMAP_ID(
                    _tilemap.bg_planes[bg].map[row][col + 1]);
                tile_l = &_tilemap.tiles[tile_id_l];
                tile_r = &_tilemap.tiles[tile_id_r];

                dx = _tilemap.origin_x + col * TILE_W;
                dy = _tilemap.origin_y + row * TILE_H;

                if (tile_l->opacity == TILE_OPAQUE &&
                    tile_r->opacity == TILE_OPAQUE) {
                    int dst_off = dy * gfx_fb.pitch + (dx >> 3);
                    asm_tile_pair_opaque(
                        gfx_fb.planes, dst_off, gfx_fb.pitch,
                        tile_l->planes[0], tile_r->planes[0],
                        TILE_PLANE_SZ);
                } else {
                    GFX_Surface tmp;
                    if (tile_l->opacity != TILE_TRANSPARENT) {
                        tile_to_surface(&tmp, tile_l);
                        if (tile_l->opacity == TILE_OPAQUE)
                            gfx_blit(dx, dy, &tmp, NULL);
                        else
                            gfx_blit_transparent(dx, dy, &tmp, NULL);
                    }
                    if (tile_r->opacity != TILE_TRANSPARENT) {
                        tile_to_surface(&tmp, tile_r);
                        if (tile_r->opacity == TILE_OPAQUE)
                            gfx_blit(dx + TILE_W, dy, &tmp, NULL);
                        else
                            gfx_blit_transparent(
                                dx + TILE_W, dy, &tmp, NULL);
                    }
                }
                drawn_tracking_mark(col, row);
                drawn_tracking_mark(col + 1, row);
            }
        }
    }

    /* 描画された列/行範囲のみ dirty rect を登録 */
    flush_drawn_dirty();
}

/* ======================================================================== */
/*  差分スクロール合成                                                      */
/*                                                                          */
/*  BB 上のタイル領域を memmove でバイト単位シフトし、                       */
/*  新規露出タイル列のみ再描画する。上層BG の非透明タイルも再描画。          */
/*  スクロールが 8px 非境界の場合や大ジャンプ時は全面再描画にフォールバック。 */
/* ======================================================================== */

/* BB のタイル領域を水平バイトシフト (NASM 高速化版) */
static void bb_shift_horiz(int shift_bytes)
{
    int base_byte = _tilemap.origin_x >> 3;
    int tile_bytes = (TILEMAP_COLS * TILE_W) >> 3;  /* 48 */
    int tile_h = TILEMAP_ROWS * TILE_H;             /* 384 */

    asm_bb_shift_horiz(
        gfx_fb.planes, gfx_fb.pitch, _tilemap.origin_y,
        tile_h, base_byte, tile_bytes, shift_bytes);
}

/* BB のタイル領域を垂直ラインシフト (NASM 高速化版) */
static void bb_shift_vert(int shift_lines)
{
    int base_byte = _tilemap.origin_x >> 3;
    int tile_bytes = (TILEMAP_COLS * TILE_W) >> 3;  /* 48 */
    int tile_h = TILEMAP_ROWS * TILE_H;             /* 384 */

    asm_bb_shift_vert(
        gfx_fb.planes, gfx_fb.pitch, _tilemap.origin_y,
        tile_h, base_byte, tile_bytes, shift_lines);
}

/* 指定列のタイルを全BG分描画 (BtF順)
 * screen_col: BB上の描画位置 (0-23)
 * map_col:    タイルマップ上の列インデックス (0-23, ラップ済み)
 */
static void draw_column_btf(int screen_col, int map_col)
{
    int row;

    if (check_can_fast(0, map_col, TILEMAP_ROWS)) {
        /* NASM ストリーム描画 (BG0 不透明・フリップなし) */
        const u8 *ptrs[TILEMAP_ROWS];
        int dx, dy;
        for (row = 0; row < TILEMAP_ROWS; row++) {
            int tid = TILEMAP_ID(
                _tilemap.bg_planes[0].map[row][map_col]);
            ptrs[row] = _tilemap.tiles[tid].planes[0];
        }
        dx = _tilemap.origin_x + screen_col * TILE_W;
        dy = _tilemap.origin_y;
        asm_draw_column_tiles(
            gfx_fb.planes, gfx_fb.pitch, dx >> 3, dy,
            ptrs, TILE_PLANE_SZ, TILEMAP_ROWS);
        for (row = 0; row < TILEMAP_ROWS; row++)
            drawn_tracking_mark(screen_col, row);
    } else {
        /* 従来パス: 共通ヘルパーで描画 */
        for (row = 0; row < TILEMAP_ROWS; row++) {
            draw_one_tile_btf(screen_col, row, map_col, row, 0, 0);
        }
    }
}

/* 指定行のタイルを全BG分描画 (BtF順)
 * screen_row: BB上の描画位置 (0-23)
 * map_row:    タイルマップ上の行インデックス (0-23, ラップ済み)
 */
static void draw_row_btf(int screen_row, int map_row)
{
    int col;

    if (check_can_fast(1, map_row, TILEMAP_COLS)) {
        const u8 *ptrs[TILEMAP_COLS];
        int dx, dy;
        for (col = 0; col < TILEMAP_COLS; col++) {
            int tid = TILEMAP_ID(
                _tilemap.bg_planes[0].map[map_row][col]);
            ptrs[col] = _tilemap.tiles[tid].planes[0];
        }
        dx = _tilemap.origin_x;
        dy = _tilemap.origin_y + screen_row * TILE_H;
        asm_draw_row_tiles(
            gfx_fb.planes, gfx_fb.pitch, dx >> 3, dy,
            ptrs, TILE_PLANE_SZ, TILEMAP_COLS);
        for (col = 0; col < TILEMAP_COLS; col++)
            drawn_tracking_mark(col, screen_row);
    } else {
        /* 従来パス: 共通ヘルパーで描画 */
        for (col = 0; col < TILEMAP_COLS; col++) {
            draw_one_tile_btf(col, screen_row, col, map_row, 0, 0);
        }
    }
}

/* 上層BG (bg >= 1) の非透明タイルを指定範囲のみ再描画 */
static void redraw_upper_bgs_partial(int col_start, int col_end,
                                     int row_start, int row_end)
{
    int row, col;
    for (row = row_start; row < row_end; row++) {
        for (col = col_start; col < col_end; col++) {
            draw_one_tile_btf(col, row, col, row, 1, 0);
        }
    }
}

/* 全タイルを強制ダーティ化 (公開API) */
void tilemap_force_redraw(void)
{
    int bg, row, col;
    if (!_tilemap.bg_planes) return;
    for (bg = 0; bg < BG_COUNT; bg++) {
        for (row = 0; row < TILEMAP_ROWS; row++) {
            for (col = 0; col < TILEMAP_COLS; col++) {
                _tilemap.bg_planes[bg].dirty[row][col] = 1;
            }
        }
    }
}

/* ======================================================================== */
/*  差分スクロール — サブ関数                                               */
/* ======================================================================== */

/* スクロール差分計算 + フォールバック判定。
 * 戻り値: 1=全面再描画が必要 (フォールバック), 0=差分処理可能 */
static int scroll_calc_delta(int *out_dx, int *out_dy, int *out_has_upper)
{
    int bg;
    int need_full = 0;

    *out_dx = _tilemap.bg_planes[0].scroll_x - _tilemap.prev_scroll_x[0];
    *out_dy = _tilemap.bg_planes[0].scroll_y - _tilemap.prev_scroll_y[0];
    *out_has_upper = 0;

    /* 上層BG のスクロール変化チェック */
    for (bg = 1; bg < BG_COUNT; bg++) {
        if (_tilemap.bg_planes[bg].scroll_x != _tilemap.prev_scroll_x[bg] ||
            _tilemap.bg_planes[bg].scroll_y != _tilemap.prev_scroll_y[bg]) {
            need_full = 1;
        }
        if (_tilemap.bg_planes[bg].visible) {
            *out_has_upper = 1;
        }
    }

    /* 8px 境界チェック */
    if ((*out_dx & 7) != 0 || (*out_dy & 7) != 0) {
        need_full = 1;
    }

    /* 大ジャンプ判定 */
    {
        int abs_dx = (*out_dx < 0) ? -*out_dx : *out_dx;
        int abs_dy = (*out_dy < 0) ? -*out_dy : *out_dy;
        if (abs_dx >= SCROLL_JUMP_THRESHOLD_X ||
            abs_dy >= SCROLL_JUMP_THRESHOLD_Y) {
            need_full = 1;
        }
    }

    return need_full;
}

/* 水平差分処理 (BBシフト + 露出列描画 + 上層BG再描画) */
static void scroll_apply_horiz(int dx_total, int has_upper_bg)
{
    int shift_bytes = dx_total >> 3;
    int abs_bytes = (shift_bytes < 0) ? -shift_bytes : shift_bytes;
    int cols_exposed, start_col, c;

    bb_shift_horiz(shift_bytes);

    cols_exposed = (abs_bytes * 8 + TILE_W - 1) / TILE_W;
    if (cols_exposed < 1) cols_exposed = 1;
    if (cols_exposed > TILEMAP_COLS) cols_exposed = TILEMAP_COLS;

    if (dx_total > 0) {
        start_col = TILEMAP_COLS - cols_exposed;
    } else {
        start_col = 0;
    }

    /* 露出列の BG0 描画 */
    {
        i16 sx = _tilemap.bg_planes[0].scroll_x;
        int first_map_col = (sx / TILE_W) % TILEMAP_COLS;
        for (c = start_col; c < start_col + cols_exposed; c++) {
            int map_col = (first_map_col + c) % TILEMAP_COLS;
            draw_column_btf(c, map_col);
        }
    }

    /* BBシフトで内容は保持されるが VRAM 上の位置がずれるため、
     * flush_drawn_dirty が正しい行範囲を包含するよう行端をマークする。
     * (drawn_tracking は列+行min/max で dirty rect を計算するため、
     *  行端をマークすれば全行がカバーされる) */
    {
        int c2;
        int kept_start = (dx_total > 0) ? 0 : cols_exposed;
        int kept_end = (dx_total > 0) ? TILEMAP_COLS - cols_exposed
                                      : TILEMAP_COLS;
        for (c2 = kept_start; c2 < kept_end; c2++) {
            drawn_tracking_mark(c2, 0);
            drawn_tracking_mark(c2, TILEMAP_ROWS - 1);
        }
    }

    /* 上層BG: 露出列のみ再描画 (保持領域はBBシフトで移動済み) */
    if (has_upper_bg) {
        redraw_upper_bgs_partial(start_col, start_col + cols_exposed,
                                 0, TILEMAP_ROWS);
    }
}

/* 垂直差分処理 (BBシフト + 露出行描画 + 上層BG再描画) */
static void scroll_apply_vert(int dy_total, int has_upper_bg)
{
    int abs_lines = (dy_total < 0) ? -dy_total : dy_total;
    int rows_exposed, start_row, r;

    bb_shift_vert(dy_total);

    rows_exposed = (abs_lines + TILE_H - 1) / TILE_H;
    if (rows_exposed < 1) rows_exposed = 1;
    if (rows_exposed > TILEMAP_ROWS) rows_exposed = TILEMAP_ROWS;

    if (dy_total > 0) {
        start_row = TILEMAP_ROWS - rows_exposed;
    } else {
        start_row = 0;
    }

    /* 露出行の BG0 描画 */
    {
        i16 sy = _tilemap.bg_planes[0].scroll_y;
        int first_map_row = (sy / TILE_H) % TILEMAP_ROWS;
        for (r = start_row; r < start_row + rows_exposed; r++) {
            int map_row = (first_map_row + r) % TILEMAP_ROWS;
            draw_row_btf(r, map_row);
        }
    }

    /* BBシフト保持領域: 行端マークで全行を dirty rect に含める
     * (理由は scroll_apply_horiz のコメント参照) */
    {
        int c2;
        for (c2 = 0; c2 < TILEMAP_COLS; c2++) {
            drawn_tracking_mark(c2, 0);
            drawn_tracking_mark(c2, TILEMAP_ROWS - 1);
        }
    }

    /* 上層BG: 露出行のみ再描画 (保持領域はBBシフトで移動済み) */
    if (has_upper_bg) {
        redraw_upper_bgs_partial(0, TILEMAP_COLS,
                                 start_row, start_row + rows_exposed);
    }
}

/* 通常の dirty タイルを処理 (スクロール以外の変更分) */
static void scroll_process_dirty(void)
{
    int row, col;
    detect_dirty();
    for (row = 0; row < TILEMAP_ROWS; row++) {
        for (col = 0; col < TILEMAP_COLS; col++) {
            if (!s_cell_dirty[row][col]) continue;
            draw_one_tile_btf(col, row, col, row, 0, 0);
        }
    }
}

/* prev_scroll を現在値に更新 */
static void scroll_update_prev(void)
{
    int bg;
    for (bg = 0; bg < BG_COUNT; bg++) {
        _tilemap.prev_scroll_x[bg] = _tilemap.bg_planes[bg].scroll_x;
        _tilemap.prev_scroll_y[bg] = _tilemap.bg_planes[bg].scroll_y;
    }
    _tilemap.scroll_changed = 0;
}

/* ======================================================================== */
/*  差分スクロール合成 (メインエントリ)                                     */
/* ======================================================================== */

void tilemap_compose_scroll(void)
{
    int dx_total, dy_total, has_upper;

    if (!_tilemap.kapi || !_tilemap.bg_planes) return;

    /* スクロール変化がなければ通常の btf で処理 */
    if (!_tilemap.scroll_changed) {
        tilemap_compose_btf();
        return;
    }

    /* 差分計算 + フォールバック判定 */
    if (scroll_calc_delta(&dx_total, &dy_total, &has_upper)) {
        /* フォールバック: 全面再描画 */
        tilemap_force_redraw();
        scroll_update_prev();
        tilemap_compose_btf();
        return;
    }

    /* ---- 差分スクロール処理 ---- */
    drawn_tracking_reset();
    gfx_dirty_suppress = 1;

    if (dx_total != 0) scroll_apply_horiz(dx_total, has_upper);
    if (dy_total != 0) scroll_apply_vert(dy_total, has_upper);

    scroll_process_dirty();

    gfx_dirty_suppress = 0;
    flush_drawn_dirty();
    scroll_update_prev();
}

void tilemap_present(void)
{
    if (_tilemap.kapi) {
        /* compose で登録済みの dirty rect のみを VRAM 転送する。
         * gfx_present_rect は全面 dirty rect を再登録してしまうため使わない。 */
        _tilemap.kapi->gfx_present_dirty();
    }
}
