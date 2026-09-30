#include "libos32gfx.h"
#include "libgfx_internal.h"
#include "os32api.h"

/* ======================================================================== */
/*  静的スプライトプール                                                    */
/* ======================================================================== */

static SprSlot *spr_slots = 0;
static SprDataPool *spr_data = 0;

/* スプライトプールの遅延初期化 (初回 gfx_create_sprite 時に確保) */
static int _spr_ensure_init(void)
{
    int i;
    if (spr_slots) return 1; /* 初期化済み */

    spr_slots = (SprSlot *)gfx_api->mem_alloc(sizeof(SprSlot) * SPR_POOL_MAX);
    spr_data = (SprDataPool *)gfx_api->mem_alloc(sizeof(SprDataPool));
    if (!spr_slots || !spr_data) {
        spr_slots = 0;
        spr_data = 0;
        return 0; /* メモリ不足 */
    }

    for (i = 0; i < SPR_POOL_MAX; i++) {
        spr_slots[i].used = 0;
        spr_slots[i].pool_type = SPR_TYPE_NONE;
    }
    for (i = 0; i < SPR_MAX_16; i++) spr_data->used_16[i] = 0;
    for (i = 0; i < SPR_MAX_32; i++) spr_data->used_32[i] = 0;
    for (i = 0; i < SPR_MAX_64; i++) spr_data->used_64[i] = 0;
    for (i = 0; i < SPR_MAX_128; i++) spr_data->used_128[i] = 0;
    return 1;
}

void gfx_sprite_init(void)
{
    /* 遅延初期化: ここでは何もしない。
     * プールは gfx_create_sprite 初回呼び出し時に確保される。
     * 既に確保済みの場合はスロットをリセットする。 */
    int i;
    if (!spr_slots) return;
    for (i = 0; i < SPR_POOL_MAX; i++) {
        spr_slots[i].used = 0;
        spr_slots[i].pool_type = SPR_TYPE_NONE;
    }
    for (i = 0; i < SPR_MAX_16; i++) spr_data->used_16[i] = 0;
    for (i = 0; i < SPR_MAX_32; i++) spr_data->used_32[i] = 0;
    for (i = 0; i < SPR_MAX_64; i++) spr_data->used_64[i] = 0;
    for (i = 0; i < SPR_MAX_128; i++) spr_data->used_128[i] = 0;
}

GFX_Sprite *gfx_create_sprite(const GFX_Surface *src, u8 trans_color)
{
    int i, p, s_idx;
    int src_pitch, pitch, sz;
    SprSlot *slot = 0;
    int slot_idx = -1;
    u8 *data_base = 0;

    if (!src) return 0;
    if (!_spr_ensure_init()) return 0; /* 遅延初期化 */

    src_pitch = src->pitch;
    pitch = (src->w + 7) / 8 + 1;
    sz = pitch * src->h;

    /* スロットを探す */
    for (i = 0; i < SPR_POOL_MAX; i++) {
        if (!spr_slots[i].used) {
            slot_idx = i;
            slot = &spr_slots[i];
            break;
        }
    }
    if (!slot) return 0;

    /* バケツを探す */
    if (sz <= SPR_SIZE_16) {
        for (i = 0; i < SPR_MAX_16; i++) {
            if (!spr_data->used_16[i]) {
                spr_data->used_16[i] = 1;
                slot->pool_type = SPR_TYPE_16;
                slot->pool_idx = i;
                /* data_base は構造体オフセットとして計算しないため、描画時に型キャストして解決する */
                break;
            }
        }
    } else if (sz <= SPR_SIZE_32) {
        for (i = 0; i < SPR_MAX_32; i++) {
            if (!spr_data->used_32[i]) {
                spr_data->used_32[i] = 1;
                slot->pool_type = SPR_TYPE_32;
                slot->pool_idx = i;
                break;
            }
        }
    } else if (sz <= SPR_SIZE_64) {
        for (i = 0; i < SPR_MAX_64; i++) {
            if (!spr_data->used_64[i]) {
                spr_data->used_64[i] = 1;
                slot->pool_type = SPR_TYPE_64;
                slot->pool_idx = i;
                break;
            }
        }
    } else if (sz <= SPR_SIZE_128) {
        for (i = 0; i < SPR_MAX_128; i++) {
            if (!spr_data->used_128[i]) {
                spr_data->used_128[i] = 1;
                slot->pool_type = SPR_TYPE_128;
                slot->pool_idx = i;
                break;
            }
        }
    }

    if (slot->pool_type == SPR_TYPE_NONE) {
        return 0; /* 空きバケツがない、またはサイズオーバー */
    }

    slot->used = 1;
    slot->spr.w = src->w;
    slot->spr.h = src->h;
    slot->spr.pitch = pitch;
    slot->spr._pool_idx = slot_idx;

    /* ポインタへのエイリアス処理マクロ（0〜7シフトの初期化用） */
#define INIT_SPRITE(DATA_ARY, MASK_ARY, BG_ARY, POOL_IDX, SIZE) \
    do { \
        for (p = 0; p < 4; p++) slot->spr.planes[p] = spr_data->DATA_ARY[POOL_IDX][0][p]; \
        slot->spr.mask = spr_data->MASK_ARY[POOL_IDX][0]; \
        slot->spr.bg_buf = (u8 *)spr_data->BG_ARY[POOL_IDX]; \
        for (s_idx = 0; s_idx < 8; s_idx++) { \
            for (i = 0; i < sz; i++) { \
                spr_data->MASK_ARY[POOL_IDX][s_idx][i] = 0xFF; \
                for (p = 0; p < 4; p++) spr_data->DATA_ARY[POOL_IDX][s_idx][p][i] = 0; \
            } \
        } \
    } while(0)

#define DRAW_SPRITE_MACRO(DATA_ARY, MASK_ARY, POOL_IDX) \
    do { \
        for (s_idx = 0; s_idx < 8; s_idx++) { \
            int ix, iy; \
            for (iy = 0; iy < src->h; iy++) { \
                for (ix = 0; ix < src->w; ix++) { \
                    int src_off = iy * src_pitch + (ix >> 3); \
                    u8 src_bit = 0x80 >> (ix & 7); \
                    u8 color = 0; \
                    for (p = 0; p < 4; p++) if (src->planes[p][src_off] & src_bit) color |= (1 << p); \
                    if (color != trans_color) { \
                        int dst_x = ix + s_idx; \
                        int dst_off = iy * pitch + (dst_x >> 3); \
                        u8 dst_bit = 0x80 >> (dst_x & 7); \
                        spr_data->MASK_ARY[POOL_IDX][s_idx][dst_off] &= ~dst_bit; \
                        for (p = 0; p < 4; p++) { \
                            if (color & (1 << p)) spr_data->DATA_ARY[POOL_IDX][s_idx][p][dst_off] |= dst_bit; \
                        } \
                    } \
                } \
            } \
        } \
    } while(0)

    if (slot->pool_type == SPR_TYPE_16) {
        INIT_SPRITE(data_16, mask_16, bg_16, slot->pool_idx, SPR_SIZE_16);
        DRAW_SPRITE_MACRO(data_16, mask_16, slot->pool_idx);
    } else if (slot->pool_type == SPR_TYPE_32) {
        INIT_SPRITE(data_32, mask_32, bg_32, slot->pool_idx, SPR_SIZE_32);
        DRAW_SPRITE_MACRO(data_32, mask_32, slot->pool_idx);
    } else if (slot->pool_type == SPR_TYPE_64) {
        INIT_SPRITE(data_64, mask_64, bg_64, slot->pool_idx, SPR_SIZE_64);
        DRAW_SPRITE_MACRO(data_64, mask_64, slot->pool_idx);
    } else if (slot->pool_type == SPR_TYPE_128) {
        INIT_SPRITE(data_128, mask_128, bg_128, slot->pool_idx, SPR_SIZE_128);
        DRAW_SPRITE_MACRO(data_128, mask_128, slot->pool_idx);
    }

    return &slot->spr;
}

void gfx_free_sprite(GFX_Sprite *spr)
{
    if (spr && spr->_pool_idx >= 0 && spr->_pool_idx < SPR_POOL_MAX) {
        SprSlot *slot = &spr_slots[spr->_pool_idx];
        if (slot->used) {
            if (slot->pool_type == SPR_TYPE_16) spr_data->used_16[slot->pool_idx] = 0;
            else if (slot->pool_type == SPR_TYPE_32) spr_data->used_32[slot->pool_idx] = 0;
            else if (slot->pool_type == SPR_TYPE_64) spr_data->used_64[slot->pool_idx] = 0;
            else if (slot->pool_type == SPR_TYPE_128) spr_data->used_128[slot->pool_idx] = 0;
            slot->used = 0;
            slot->pool_type = SPR_TYPE_NONE;
        }
    }
}

extern void __cdecl asm_gfx_draw_sprite_core(int x, int y, int w, int h, int src_pitch, const u8 **planes, const u8 *mask, u8 **bb_array);

/* パックド 8bpp のスプライト転送 (票 H2b)。
 * スプライトのデータ自体は 4 プレーン + マスクのまま (x&7 でシフト済みの
 * 8 通りを持つ形式) で、ここで 1 画素ずつ色を組み立てて画面へ置く。
 * 画面の左端バイト列は x & ~7 — シフト済みデータの位置に合わせる。
 * mask のビットが 1 = 透過 (書かない)。 */
static void gfx_draw_sprite_packed(int x, int y, int h, int pitch,
                                   const u8 **planes, const u8 *mask)
{
    int cols = pitch * 8;
    int basex = x & ~7;
    int iy, col, p;

    for (iy = 0; iy < h; iy++) {
        int py = y + iy;
        int row_off;
        if (py < 0 || py >= gfx_fb.height) continue;
        row_off = iy * pitch;
        for (col = 0; col < cols; col++) {
            int off = row_off + (col >> 3);
            u8 bit = (u8)(0x80 >> (col & 7));
            int px;
            u8 color;
            if (mask[off] & bit) continue;      /* 透過 */
            px = basex + col;
            if (px < 0 || px >= gfx_fb.width) continue;
            color = 0;
            for (p = 0; p < 4; p++) {
                if (planes[p][off] & bit) color |= (u8)(1 << p);
            }
            gfx_fb.planes[0][py * gfx_fb.pitch + px] = color;
        }
    }
}

void gfx_draw_sprite(int x, int y, const GFX_Sprite *spr)
{
    if (!spr) return;
    if (spr->_pool_idx >= 0 && spr->_pool_idx < SPR_POOL_MAX) {
        SprSlot *slot = &spr_slots[spr->_pool_idx];
        if (slot->used) {
            int shift = x & 7;
            const u8 *planes[4];
            const u8 *mask = 0;
            int p;

            if (slot->pool_type == SPR_TYPE_16) {
                for(p=0; p<4; p++) planes[p] = spr_data->data_16[slot->pool_idx][shift][p];
                mask = spr_data->mask_16[slot->pool_idx][shift];
            } else if (slot->pool_type == SPR_TYPE_32) {
                for(p=0; p<4; p++) planes[p] = spr_data->data_32[slot->pool_idx][shift][p];
                mask = spr_data->mask_32[slot->pool_idx][shift];
            } else if (slot->pool_type == SPR_TYPE_64) {
                for(p=0; p<4; p++) planes[p] = spr_data->data_64[slot->pool_idx][shift][p];
                mask = spr_data->mask_64[slot->pool_idx][shift];
            } else if (slot->pool_type == SPR_TYPE_128) {
                for(p=0; p<4; p++) planes[p] = spr_data->data_128[slot->pool_idx][shift][p];
                mask = spr_data->mask_128[slot->pool_idx][shift];
            }

            if (mask) {
                if (gfx_packed)
                    gfx_draw_sprite_packed(x, y, spr->h, spr->pitch, planes, mask);
                else
                    asm_gfx_draw_sprite_core(x, y, spr->w, spr->h, spr->pitch, planes, mask, gfx_fb.planes);
            }
        }
    }
    gfx_api->gfx_add_dirty_rect(x, y, spr->w, spr->h);
}

void gfx_sprite_save_bg(int x, int y, GFX_Sprite *spr)
{
    if (!spr || !spr->bg_buf) return;
    gfx_save_rect(x, y, spr->w, spr->h, spr->bg_buf);
}

void gfx_sprite_restore_bg(int x, int y, GFX_Sprite *spr)
{
    if (!spr || !spr->bg_buf) return;
    gfx_restore_rect(x, y, spr->w, spr->h, spr->bg_buf);
    gfx_api->gfx_add_dirty_rect(x, y, spr->w, spr->h);
}
