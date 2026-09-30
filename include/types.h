/* ======================================================================== */
/*  TYPES.H — OS32 共通型定義                                               */
/*                                                                          */
/*  全モジュールで使用する基本型を定義する。                                 */
/*  os32_kapi_shared.h が先にインクルードされている場合は重複定義しない。     */
/* ======================================================================== */

#ifndef TYPES_H
#define TYPES_H

/* os32_kapi_shared.h で既に定義されている場合はスキップ */
#ifndef OS32_KAPI_SHARED_H

#ifndef __cdecl
#define __cdecl __attribute__((cdecl))
#endif

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned long  u32;
typedef signed char    i8;
typedef signed short   i16;
typedef signed long    i32;

/* NULLポインタ */
#ifndef NULL
#define NULL ((void *)0)
#endif

#endif /* OS32_KAPI_SHARED_H */

/* コンパイル時アサーション (C89)。
 * cond が偽だと「負サイズの配列」でコンパイルエラーになる。
 * 二重定義に見える定数の一致 (SHM サイズ等) やレイアウトの隣接関係は
 * コメントでなくこれで担保する。 */
#ifndef STATIC_ASSERT
#define STATIC_ASSERT(cond, name) \
    typedef char static_assert_##name[(cond) ? 1 : -1]
#endif

#endif /* TYPES_H */

