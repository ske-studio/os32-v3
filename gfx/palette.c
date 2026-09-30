/* ======================================================================== */
/*  PALETTE.C — PC-98 アナログパレット管理 (シャドウパレット方式)            */
/*                                                                          */
/*  出典: PC9800Bible §2-7                                                  */
/* ======================================================================== */

#include "palette.h"
#include "gfx.h"   /* PAL_IDX_PORT, PAL_G_PORT, PAL_R_PORT, PAL_B_PORT */
#include "io.h"

/* シャドウパレット */
static PaletteEntry shadow[PALETTE_COUNT];

/* PC-98デフォルト16色パレット */
static const PaletteEntry default_palette[PALETTE_COUNT] = {
    /* idx  R   G   B   色名 */
    {  0,  0,  0 },  /*  0: 黒 */
    {  0,  0,  7 },  /*  1: 青 */
    {  7,  0,  0 },  /*  2: 赤 */
    {  7,  0,  7 },  /*  3: 紫 */
    {  0,  7,  0 },  /*  4: 緑 */
    {  0,  7,  7 },  /*  5: 水色 */
    {  7,  7,  0 },  /*  6: 黄色 */
    {  7,  7,  7 },  /*  7: 白(暗) */
    {  0,  0,  0 },  /*  8: 黒(明) */
    {  0,  0, 15 },  /*  9: 明青 */
    { 15,  0,  0 },  /* 10: 明赤 */
    { 15,  0, 15 },  /* 11: 明紫 */
    {  0, 15,  0 },  /* 12: 明緑 */
    {  0, 15, 15 },  /* 13: 明水色 */
    { 15, 15,  0 },  /* 14: 明黄 */
    { 15, 15, 15 },  /* 15: 白(明) */
};

/* ハードウェアにパレットを書き込み */
static void hw_set_palette(int idx, u8 r, u8 g, u8 b)
{
    outp(PAL_IDX_PORT, (u8)idx);
    outp(PAL_G_PORT, g & 0x0F);
    outp(PAL_R_PORT, r & 0x0F);
    outp(PAL_B_PORT, b & 0x0F);
}

void palette_init(void)
{
    int i;
    for (i = 0; i < PALETTE_COUNT; i++) {
        shadow[i] = default_palette[i];
        hw_set_palette(i, shadow[i].r, shadow[i].g, shadow[i].b);
    }
}

void palette_set(int idx, u8 r, u8 g, u8 b)
{
    if (idx < 0 || idx >= PALETTE_COUNT) return;
    shadow[idx].r = r & 0x0F;
    shadow[idx].g = g & 0x0F;
    shadow[idx].b = b & 0x0F;
    hw_set_palette(idx, shadow[idx].r, shadow[idx].g, shadow[idx].b);
}

/* シャドウだけを更新する (ハードウェアには書かない)。
 * 256 色バックエンド (PEGC) はパレットレジスタの輝度が 8bit で、この
 * ファイルの hw_set_palette (4bit) を通せない。それでも KAPI の
 * gfx_get_palette は palette_get = シャドウを読むので、バックエンド側が
 * 0〜15 のシステム色を書いたときはここで台帳を合わせる (票 H2c)。 */
void palette_shadow_set(int idx, u8 r, u8 g, u8 b)
{
    if (idx < 0 || idx >= PALETTE_COUNT) return;
    shadow[idx].r = r & 0x0F;
    shadow[idx].g = g & 0x0F;
    shadow[idx].b = b & 0x0F;
}

void palette_get(int idx, u8 *r, u8 *g, u8 *b)
{
    if (idx < 0 || idx >= PALETTE_COUNT) return;
    if (r) *r = shadow[idx].r;
    if (g) *g = shadow[idx].g;
    if (b) *b = shadow[idx].b;
}

const PaletteEntry *palette_get_all(void)
{
    return shadow;
}
