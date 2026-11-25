#ifndef KAPI_PROFILE_H
#define KAPI_PROFILE_H

/* ======================================================================== */
/*  kapi_profile.h — KAPI 呼び出し回数カウンタ (計測ビルド専用)              */
/*                                                                          */
/*  生成器 (sdk/gen_kapi.py) が各 __cdecl ラッパの先頭に KAPI_HIT(slot) を   */
/*  吐く。-DKAPI_PROFILE を付けたビルドでだけ slot 別カウンタが増え、既定   */
/*  では何も生成されない (ABI・サイズとも不変)。                             */
/*                                                                          */
/*  用途: リング 3 化の可否判断。INT ゲート遷移が 1 回 ~6us かかるため、    */
/*  実プログラムが 1 秒に何回 KAPI を叩くかが成立条件になる。               */
/*                                                                          */
/*  読み出し: kernel.map の kapi_hits の番地を                               */
/*    GET /api/mem?addr=<addr>&len=<4*KAPI_FUNC_COUNT>&space=phys            */
/*  で引き、slot 番号は sdk/kapi.json の api 配列の並び順。                  */
/*                                                                          */
/*  ビルド: make kernel KERNEL_CFLAGS_EXTRA=-DKAPI_PROFILE                   */
/* ======================================================================== */

#include "types.h"

#ifdef KAPI_PROFILE
extern volatile u32 kapi_hits[];
#define KAPI_HIT(slot)  (kapi_hits[(slot)]++)
#else
#define KAPI_HIT(slot)  ((void)0)
#endif

#endif /* KAPI_PROFILE_H */
