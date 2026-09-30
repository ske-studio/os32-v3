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

/* パレットエントリ (各4bit: 0-15) */
typedef struct {
    u8 r;   /* 赤輝度 (0-15) */
    u8 g;   /* 緑輝度 (0-15) */
    u8 b;   /* 青輝度 (0-15) */
} PaletteEntry;

#define PALETTE_COUNT 16   /* PC-98の標準16色パレット数 */

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
