#include "tilemap_internal.h"

void tilemap_set(int bg, int col, int row, u16 tile_attr)
{
    if (!_tilemap.bg_planes || bg < 0 || bg >= BG_COUNT) return;
    if (col < 0 || col >= TILEMAP_COLS || row < 0 || row >= TILEMAP_ROWS) return;

    if (_tilemap.bg_planes[bg].map[row][col] != tile_attr) {
        _tilemap.bg_planes[bg].map[row][col] = tile_attr;
        _tilemap.bg_planes[bg].dirty[row][col] = 1;
    }
}

u16 tilemap_get(int bg, int col, int row)
{
    if (!_tilemap.bg_planes || bg < 0 || bg >= BG_COUNT) return 0;
    if (col < 0 || col >= TILEMAP_COLS || row < 0 || row >= TILEMAP_ROWS) return 0;

    return _tilemap.bg_planes[bg].map[row][col];
}

void tilemap_fill(int bg, u16 tile_attr)
{
    int row, col;
    if (!_tilemap.bg_planes || bg < 0 || bg >= BG_COUNT) return;

    for (row = 0; row < TILEMAP_ROWS; row++) {
        for (col = 0; col < TILEMAP_COLS; col++) {
            if (_tilemap.bg_planes[bg].map[row][col] != tile_attr) {
                _tilemap.bg_planes[bg].map[row][col] = tile_attr;
                _tilemap.bg_planes[bg].dirty[row][col] = 1;
            }
        }
    }
}

void tilemap_set_visible(int bg, int visible)
{
    int row, col;
    if (!_tilemap.bg_planes || bg < 0 || bg >= BG_COUNT) return;

    if (_tilemap.bg_planes[bg].visible != visible) {
        _tilemap.bg_planes[bg].visible = visible;
        /* 表示状態が変わった場合は全タイルを再描画の対象とするため、
         * 強制的にこのBGのdirtyフラグを立てる */
        for (row = 0; row < TILEMAP_ROWS; row++) {
            for (col = 0; col < TILEMAP_COLS; col++) {
                _tilemap.bg_planes[bg].dirty[row][col] = 1;
            }
        }
    }
}

void tilemap_scroll(int bg, int sx, int sy)
{
    if (!_tilemap.bg_planes || bg < 0 || bg >= BG_COUNT) return;

    if (_tilemap.bg_planes[bg].scroll_x != sx ||
        _tilemap.bg_planes[bg].scroll_y != sy) {
        _tilemap.bg_planes[bg].scroll_x = sx;
        _tilemap.bg_planes[bg].scroll_y = sy;
        /* compose側でBBシフト+端列dirty化を行うためフラグのみセット */
        _tilemap.scroll_changed = 1;
    }
}

void tilemap_scroll_hw(int lines)
{
    if (!_tilemap.kapi) return;
    _tilemap.kapi->gfx_hardware_scroll(lines);
}

void tilemap_scroll_sync(void)
{
    int bg;
    if (!_tilemap.bg_planes) return;
    for (bg = 0; bg < BG_COUNT; bg++) {
        _tilemap.prev_scroll_x[bg] = _tilemap.bg_planes[bg].scroll_x;
        _tilemap.prev_scroll_y[bg] = _tilemap.bg_planes[bg].scroll_y;
    }
    _tilemap.scroll_changed = 0;
}
