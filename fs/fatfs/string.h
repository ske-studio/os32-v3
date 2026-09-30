/* ======================================================================== */
/*  STRING.H — FatFs用 string.h スタブ (OS32 freestanding環境)              */
/*                                                                          */
/*  FatFs (ff.c) が #include <string.h> するが、OS32カーネルは               */
/*  freestanding環境のため標準ライブラリが存在しない。                       */
/*  kstring.h が memcpy/memset/memcmp 等の標準名シンボルを提供するので       */
/*  それを転送するだけでよい。                                               */
/* ======================================================================== */

#ifndef _FATFS_STRING_H_STUB
#define _FATFS_STRING_H_STUB

#include "kstring.h"

/* kstring.h が以下の標準名を既に提供済み:
 *   memcpy, memset, memcmp, memmove, strlen, strcmp, strncmp, strchr
 * FatFs (ff.c) はこれらを直接呼び出さないが、GCCが最適化時に生成する
 * 場合に備えて kstring.h 経由で利用可能にする。
 */

#endif /* _FATFS_STRING_H_STUB */
