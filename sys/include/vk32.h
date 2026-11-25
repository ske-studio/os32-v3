/* ======================================================================== */
/*  VK32.H -- VK32 圧縮カーネルイメージヘッダ                                */
/*                                                                          */
/*  vmkernel.lz4 ファイルの先頭に配置されるヘッダ構造体。                    */
/*  カーネルローダー・ビルドツール・インストーラーが共通で使用する。          */
/*                                                                          */
/*  フォーマット (v2、票 TASK_SERIAL_HOSTFS A-4。正典は boot/boot_defs.h):   */
/*    [共通部 16B] [VK32Entry * n] [entry_crc * n] [image_size] [image_crc]  */
/*    [LZ4圧縮データ...]                                                     */
/*  image_crc はファイル全体の CRC32 (この欄を 0 として計算)。               */
/*                                                                          */
/*  header_size フィールドがデータ開始オフセットを兼ねる。                    */
/*  data_offset はファイル先頭からの絶対オフセット。                          */
/* ======================================================================== */

#ifndef VK32_H
#define VK32_H

#include "types.h"

/* マジックナンバー: 'VK32' (リトルエンディアン) */
#define VK32_MAGIC       0x32334B56UL
#define VK32_VERSION     2
#define VK32_MAX_ENTRIES 4

/* エントリ: 個別の圧縮モジュール情報 */
typedef struct {
    u32 load_addr;         /* 展開先アドレス */
    u32 raw_size;          /* 展開後サイズ (バイト) */
    u32 data_offset;       /* LZ4データ位置 (ファイル先頭からの絶対オフセット) */
    u32 compressed_size;   /* LZ4圧縮データサイズ */
} VK32Entry;

/* ヘッダ: ファイル先頭に配置 */
typedef struct {
    u32 magic;             /* VK32_MAGIC */
    u32 header_size;       /* = 16 + entry_count * 20 + 8 (データ開始位置) */
    u32 version;           /* VK32_VERSION */
    u32 entry_count;       /* エントリ数 */
    /*
     * 実際のエントリ数は entry_count 個。エントリ表の後ろに CRC 表が続くので
     * この構造体の entries[] を entry_count 超で読まないこと。
     * ファイル上のヘッダサイズは header_size で決まり、
     * sizeof(VK32Header) とは一致しない場合がある。
     * ローダーは header_size を基準にデータ開始位置を決定すること。
     */
    VK32Entry entries[VK32_MAX_ENTRIES];
} VK32Header;

#endif /* VK32_H */
