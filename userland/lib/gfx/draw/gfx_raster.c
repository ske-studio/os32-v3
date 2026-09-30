/* ======================================================================== */
/*  GFX_RASTER.C — ラスタパレットユーティリティ (libos32gfx)                */
/*                                                                          */
/*  外部プログラム向け: ラスタパレットテーブルの構築ヘルパーと                */
/*  KAPI経由のラスタパレット付きVRAM転送のラッパー。                         */
/* ======================================================================== */

#include "libos32gfx.h"

/* ラスタパレットテーブルをクリア */
void gfx_raster_clear(GFX_RasterPalTable *table)
{
    if (!table) return;
    table->count = 0;
}

/* ラスタパレットテーブルにエントリを追加
 * line は昇順で追加すること (ソート済み前提)
 * 戻り値: 0=成功, -1=テーブル満杯 */
int gfx_raster_add(GFX_RasterPalTable *table,
                    int line, int pal_idx, u8 r, u8 g, u8 b)
{
    GFX_RasterPalEntry *e;

    if (!table) return -1;
    if (table->count >= GFX_RASTER_MAX_ENTRIES) return -1;

    e = &table->entries[table->count];
    e->line = (u16)line;
    e->pal_idx = (u8)pal_idx;
    e->r = r & 0x0F;
    e->g = g & 0x0F;
    e->b = b & 0x0F;
    table->count++;

    return 0;
}

/* ラスタパレットのみ適用 (VRAM転送なし)
 * バックバッファが変更されていない場合に使う。
 * VSYNC同期 + HBLANK同期パレット書き換えのみ行う。 */
void gfx_present_raster_only(GFX_RasterPalTable *table)
{
    gfx_api->gfx_present_raster(table);
}

/* VRAM転送 + ラスタパレット適用
 * バックバッファが変更された場合に使う。
 * dirty rect全画面登録 → KAPI gfx_present_raster で転送+パレット書き換え */
void gfx_present_with_raster(GFX_RasterPalTable *table)
{
    gfx_api->gfx_add_dirty_rect(0, 0, 640, 400);
    gfx_api->gfx_present_raster(table);
}
