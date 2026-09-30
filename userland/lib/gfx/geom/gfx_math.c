/* ======================================================================== */
/*  GFX_MATH.C — 整数 sin/cos 互換ラッパー (libos32gfx)                    */
/*                                                                          */
/*  実体は libos32math/trig.c に移設済み。                                   */
/*  既存の libos32gfx コードとの後方互換性のために、gfx_* プレフィックス付き */
/*  の薄いラッパーを提供する。                                               */
/* ======================================================================== */

#include "libos32gfx.h"
#include "libos32math.h"

i32 gfx_isin(int angle)    { return isin(angle); }
i32 gfx_icos(int angle)    { return icos(angle); }
int gfx_deg_to_idx(int deg) { return deg_to_idx(deg); }
