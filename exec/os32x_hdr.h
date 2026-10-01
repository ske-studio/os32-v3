/* D35: complete OS32X v4 header, known flags and independent generations.
 * Pure admission predicate shared by app, resident shell and shlib loaders. */
#ifndef __OS32X_HDR_H
#define __OS32X_HDR_H

#include "os32_kapi_shared.h"

#define OS32X_LAYOUT_OK        0   /* complete matching header */
#define OS32X_LAYOUT_SHORT     1   /* fewer than a complete header */
#define OS32X_LAYOUT_OLD       2   /* unknown format/header size/flags */
#define OS32X_LAYOUT_MISMATCH  3   /* generation/layout/feature mismatch */

/* hdr      : ファイル先頭 (read_len バイトが有効)
 * read_len : 実際に読めたバイト数 (ヘッダを読んだ vfs_read の戻り値)
 * kernel_off : 動いているカーネルの KAPI_DATA_FIELDS_OFF
 * 戻り値   : OS32X_LAYOUT_* (0 = 通す) */
int os32x_layout_check(const OS32Header *hdr, u32 read_len, u32 kernel_off);

/* 断った理由の短い英語 (表示用、固定文字列)。 */
const char *os32x_layout_reason(int rc);

#endif
