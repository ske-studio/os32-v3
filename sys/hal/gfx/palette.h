/* ======================================================================== */
/*  PALETTE.H — PC-98 アナログパレット管理                                  */
/*                                                                          */
/*  PC-98のアナログパレットは書き込み専用I/Oのため、                         */
/*  ソフトウェアでシャドウパレットを保持して管理する。                       */
/*                                                                          */
/*  I/Oポート:                                                              */
/*    0xA8: パレット番号 (0-15)                                             */
/*    0xAA: 緑(G)輝度 (0-15)                                               */
/*    0xAC: 赤(R)輝度 (0-15)                                               */
/*    0xAE: 青(B)輝度 (0-15)                                               */
/*                                                                          */
/*  出典: PC9800Bible §2-7                                                  */
/* ======================================================================== */

#ifndef PALETTE_H
#define PALETTE_H

#include "types.h"
#include "gfx_format.h"

/* パレットエントリ (各4bit: 0-15) */
typedef struct {
    u8 r;   /* 赤輝度 (0-15) */
    u8 g;   /* 緑輝度 (0-15) */
    u8 b;   /* 青輝度 (0-15) */
} PaletteEntry;

/* HAL の RGB の組と、KAPI の 4bit のパレット輝度。 */
#define GFX_RGB_COMPONENTS 3
#define GFX_RGB_R 0
#define GFX_RGB_G 1
#define GFX_RGB_B 2
#define GFX_PALETTE_COMPONENT_BITS 4     /* KAPI の成分幅 (PEGC の側は sys/platform/pc98/pegc.h の PEGC_PAL_KAPI_BITS と同じ値) */
#define GFX_PALETTE_COMPONENT_MASK 0x0F  /* KAPI 側の輝度は 4bit (0〜15) */
#define GFX_PALETTE_COMPONENT_SCALE 17   /* 4bit → 8bit の伸張係数 (15*17 = 255) */
_Static_assert(GFX_PALETTE_COMPONENT_MASK == (1u << GFX_PALETTE_COMPONENT_BITS) - 1u &&
               GFX_PALETTE_COMPONENT_MASK * GFX_PALETTE_COMPONENT_SCALE == 255u, "palette component width, mask and scale agree");
#define GFX_PALETTE_CUBE_LEVELS 6
#define GFX_PALETTE_GRAY_FIRST 8
#define GFX_PALETTE_GRAY_STEP 10


/* シャドウを既定の16色に戻す (ハードウェアには書かない)。 */
void palette_shadow_reset(void);

/* パレット初期化 (デフォルト16色セット) */
void palette_init(void);

/* パレット設定 (ハードウェア+シャドウ同時更新) */
void palette_set(int idx, u8 r, u8 g, u8 b);

/* シャドウのみ更新 (ハードウェアには書かない)。輝度が 8bit の 256 色
 * バックエンドが、自分で書いた 0〜15 のシステム色を台帳へ反映するために使う。 */
void palette_shadow_set(int idx, u8 r, u8 g, u8 b);

/* シャドウパレット取得 */
void palette_get(int idx, u8 *r, u8 *g, u8 *b);

/* 全パレット取得 (16エントリ分) */
const PaletteEntry *palette_get_all(void);

#endif /* PALETTE_H */
