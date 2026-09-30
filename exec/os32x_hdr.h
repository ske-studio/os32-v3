/* ======================================================================== */
/*  OS32X_HDR.H — OS32X ヘッダ v3 の配置照合 (票 TASK_KAPI_DATA_FIELDS)     */
/*                                                                          */
/*  KAPI v63 でデータ欄 (sbrk_heap_limit / shm_base) を固定オフセット        */
/*  KAPI_DATA_FIELDS_OFF へ移した。ビルド時の配置は OS32X ヘッダ v3 の        */
/*  kapi_data_off に焼かれている (生成器が ELF の .os32_kapi_layout から写す)。*/
/*  exec (アプリ・常駐シェル) と shlib ローダはここで照合し、違えば断る。    */
/*                                                                          */
/*  判定だけの純関数 — ホスト試験 (tools/tests/os32x_layout_host.c) が        */
/*  そのまま呼ぶので、カーネル内部ヘッダに依存しない。                        */
/* ======================================================================== */

#ifndef __OS32X_HDR_H
#define __OS32X_HDR_H

#include "os32_kapi_shared.h"

#define OS32X_LAYOUT_OK        0   /* v3 で、配置がカーネルと一致 */
#define OS32X_LAYOUT_SHORT     1   /* 読めた長さ / header_size が v3 に届かない */
#define OS32X_LAYOUT_OLD       2   /* ヘッダ v2 以前 (配置を示す欄が無い) */
#define OS32X_LAYOUT_MISMATCH  3   /* kapi_data_off がカーネルと違う */

/* hdr      : ファイル先頭 (read_len バイトが有効)
 * read_len : 実際に読めたバイト数 (ヘッダを読んだ vfs_read の戻り値)
 * kernel_off : 動いているカーネルの KAPI_DATA_FIELDS_OFF
 * 戻り値   : OS32X_LAYOUT_* (0 = 通す) */
int os32x_layout_check(const OS32Header *hdr, u32 read_len, u32 kernel_off);

/* 断った理由の短い英語 (表示用、固定文字列)。 */
const char *os32x_layout_reason(int rc);

#endif
