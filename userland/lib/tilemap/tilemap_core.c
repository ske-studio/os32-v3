#include <string.h>
#include "tilemap_internal.h"
#include "libos32asset.h"

TilemapState _tilemap;

void tilemap_init(KernelAPI *api)
{
    int i;
    if (!api) return;

    memset(&_tilemap, 0, sizeof(_tilemap));
    _tilemap.kapi = api;

    /* メモリ確保 */
    _tilemap.tiles = (TileDef *)api->mem_alloc(sizeof(TileDef) * MAX_TILES);
    _tilemap.bg_planes = (BGPlane *)api->mem_alloc(sizeof(BGPlane) * BG_COUNT);

    if (!_tilemap.tiles || !_tilemap.bg_planes) {
        api->kprintf(ATTR_RED, "tilemap_init: Out of memory\r\n");
        return;
    }

    memset(_tilemap.tiles, 0, sizeof(TileDef) * MAX_TILES);
    memset(_tilemap.bg_planes, 0, sizeof(BGPlane) * BG_COUNT);

    /* 初期状態: BG0のみ表示、他は非表示 */
    _tilemap.bg_planes[0].visible = 1;
    for (i = 1; i < BG_COUNT; i++) {
        _tilemap.bg_planes[i].visible = 0;
    }

    _tilemap.origin_x = 0;
    _tilemap.origin_y = 0;

    /* GFX初期化 */
    libos32gfx_init(api);
}

void tilemap_shutdown(void)
{
    if (!_tilemap.kapi) return;

    if (_tilemap.tiles) {
        _tilemap.kapi->mem_free(_tilemap.tiles);
    }
    if (_tilemap.bg_planes) {
        _tilemap.kapi->mem_free(_tilemap.bg_planes);
    }

    libos32gfx_shutdown();
    memset(&_tilemap, 0, sizeof(_tilemap));
}

void tilemap_define(int id, const u8 *data_4bpp)
{
    int row, col, p;
    TileDef *t;
    int has_transparent = 0;
    int has_opaque = 0;

    if (!_tilemap.tiles || id < 0 || id >= MAX_TILES || !data_4bpp) return;

    t = &_tilemap.tiles[id];
    memset(t->planes, 0, sizeof(t->planes));

    /* 4bppパックド(128 bytes) -> 4プレーン(32 bytes * 4) 変換 */
    for (row = 0; row < TILE_H; row++) {
        for (col = 0; col < TILE_W; col++) {
            /* 1ピクセルずつ取り出してプレーンに書き込む */
            int data_idx = (row * TILE_W + col) / 2;
            u8 byte_val = data_4bpp[data_idx];
            u8 color;
            int bit_idx = 7 - (col % 8);
            int dest_idx = row * TILE_PITCH + (col / 8);

            if (col % 2 == 0) {
                color = (byte_val >> 4) & 0x0F;
            } else {
                color = byte_val & 0x0F;
            }

            if (color == 0) {
                has_transparent = 1;
            } else {
                has_opaque = 1;
            }

            for (p = 0; p < 4; p++) {
                if (color & (1 << p)) {
                    t->planes[p][dest_idx] |= (1 << bit_idx);
                }
            }
        }
    }

    /* 不透明度の自動判定 */
    if (!has_opaque) {
        t->opacity = TILE_TRANSPARENT; /* 全部色0 */
    } else if (!has_transparent) {
        t->opacity = TILE_OPAQUE;      /* 全部色あり */
    } else {
        t->opacity = TILE_PARTIAL;     /* 混在 */
    }
}

int tilemap_load(const char *path, int start_id)
{
    int fd, file_sz, tile_count, i;
    u8 buf[128]; /* 1タイル分の4bppデータ */
    int bytes_read;

    if (!_tilemap.kapi || !path || start_id < 0) return -1;

    fd = _tilemap.kapi->sys_open(path, O_RDONLY);
    if (fd < 0) return -1;

    /* ファイルサイズを取得: sys_lseek(SEEK_END) は新しい位置を返す */
    file_sz = _tilemap.kapi->sys_lseek(fd, 0, SEEK_END);
    _tilemap.kapi->sys_lseek(fd, 0, SEEK_SET);

    if (file_sz <= 0) {
        _tilemap.kapi->sys_close(fd);
        return -1;
    }

    tile_count = file_sz / 128;
    if (start_id + tile_count > MAX_TILES) {
        tile_count = MAX_TILES - start_id;
    }

    for (i = 0; i < tile_count; i++) {
        bytes_read = _tilemap.kapi->sys_read(fd, buf, 128);
        if (bytes_read < 128) break;
        tilemap_define(start_id + i, buf);
    }

    _tilemap.kapi->sys_close(fd);
    return i;
}

/* asset経由タイルセットロード (キャッシュ対応)
 * 戻り値: ロードしたタイル数、失敗時 -1
 * アセットハンドルは呼び出し側で管理 (解放はasset_release)
 */
int tilemap_load_asset(asset_handle_t h, int start_id)
{
    const u8 *raw;
    u32 file_sz;
    int tile_count, i;

    if (!_tilemap.kapi || start_id < 0) return -1;
    if (h < 0) return -1;

    /* アセットがREADYか確認 */
    if (asset_state(h) != ASSET_STATE_READY) return -1;

    raw = (const u8 *)asset_data(h);
    file_sz = asset_size(h);
    if (!raw || file_sz == 0) return -1;

    tile_count = (int)(file_sz / 128);
    if (start_id + tile_count > MAX_TILES) {
        tile_count = MAX_TILES - start_id;
    }

    for (i = 0; i < tile_count; i++) {
        tilemap_define(start_id + i, raw + i * 128);
    }

    return i;
}

void tilemap_set_origin(int ox, int oy)
{
    /* バイト境界(8の倍数)に揃える */
    _tilemap.origin_x = ox & ~7;
    _tilemap.origin_y = oy;
}

void tilemap_set_palette(const u8 pal[16][3])
{
    int i;
    if (!_tilemap.kapi || !pal) return;

    for (i = 0; i < 16; i++) {
        _tilemap.kapi->gfx_set_palette(i, pal[i][0], pal[i][1], pal[i][2]);
    }
}
