/* ======================================================================== */
/*  ISO9660.H — ISO 9660 読み取り専用ファイルシステム                        */
/*                                                                          */
/*  CD-ROM (ISO 9660) のディレクトリ走査・ファイル読み出しをVFS経由で行う。   */
/*  Level 1 (8.3ファイル名) のみ対応。Rock Ridge / Joliet 拡張は非対応。    */
/*  ファイル名の大文字/小文字は区別しない (case-insensitive比較)。           */
/*                                                                          */
/*  セクタサイズ: 2048バイト (ATAPI_SECTOR_SIZE)                            */
/*  Primary Volume Descriptor: LBA 16                                       */
/* ======================================================================== */

#ifndef ISO9660_H
#define ISO9660_H

#include "vfs.h"
#include "memmap.h"   /* PIT_HZ */
#include "atapi.h"    /* ATAPI_READ_MAX_SECTORS */

/* ISO 9660 定数 */
#define ISO_SECTOR_SIZE       2048
#define ISO_PVD_LBA           16      /* Primary Volume Descriptor の位置 */
#define ISO_PVD_MAGIC         "CD001" /* PVD識別文字列 (5バイト) */
#define ISO_MAX_NAME          128     /* ファイル名最大長 */

/* === Primary Volume Descriptor (LBA 16, 2048バイト) ===
 *
 * オフセットと主要フィールド:
 *   [0]       Type Code: 1 = Primary
 *   [1-5]     "CD001"
 *   [6]       Version: 1
 *   [8-39]    System Identifier
 *   [40-71]   Volume Identifier
 *   [80-87]   Volume Space Size (both-endian u32)
 *   [120-123] Logical Block Size (LE u16 @ 128)
 *   [128-131] Logical Block Size (both-endian u16)
 *   [156-189] Root Directory Record (34バイト)
 */

/* ディレクトリレコード (ISO 9660 §9.1)
 *
 * 可変長 (最小33バイト + ファイル名長):
 *   [0]       Record Length
 *   [1]       Extended Attribute Record Length
 *   [2-9]     Extent Location (both-endian u32)
 *   [10-17]   Data Length (both-endian u32)
 *   [18-24]   Recording Date/Time
 *   [25]      File Flags: bit1=ディレクトリ
 *   [26]      File Unit Size
 *   [27]      Interleave Gap Size
 *   [28-31]   Volume Sequence Number (both-endian u16)
 *   [32]      File Identifier Length
 *   [33+]     File Identifier
 */

/* ファイルフラグ (ディレクトリレコード [25]) */
#define ISO_FLAG_HIDDEN      0x01
#define ISO_FLAG_DIRECTORY   0x02
#define ISO_FLAG_ASSOCIATED  0x04
#define ISO_FLAG_RECORD      0x08
#define ISO_FLAG_PROTECTION  0x10
#define ISO_FLAG_MULTIEXTENT 0x80

/* ======== 読みのキャッシュ (票: 実機の cdinst が 20KB/s を切った件) ========
 *
 * VFS は読みのたびにパスを渡すので、以前は区切りごとに根からディレクトリを
 * 読み直していた (データと離れたディレクトリのセクタへ毎回シークする)。
 *   - 直前に解決したパス 1 本の結果 (LBA・サイズ・フラグ) を覚える
 *   - ディレクトリのセクタと、セクタの一部だけを読む読みのセクタを
 *     ISO_SCACHE_SLOTS 本の LRU で持つ
 *   - ファイルのデータは先読みの窓 (ISO_RA_SECTORS 本 = 1 回の READ(10)) を
 *     通す。4KB ずつの読みが続いても READ(10) は窓 1 枚につき 1 回
 *   - 窓より大きい、セクタに揃った範囲は呼び手のバッファへまとめて直接読む
 *     (atapi が ATAPI_READ_MAX_SECTORS ずつの READ(10) に分ける)
 * 捨てる合図は (1) umount (ctx ごと捨てる)、(2) ATAPI の媒体の世代
 * (UNIT ATTENTION / NOT READY を見ると進む)、(3) 最後に媒体を読んでから
 * ISO_IDLE_TICKS を超えて空いたとき (FD の「2 秒規則」と同じ考え方)。
 * **CD を入れ替えたら umount / mount する** のが約束 (docs/06_filesystem.md §6-6)。 */
#define ISO_SCACHE_SLOTS      4       /* 8KB (kzalloc した ctx の中に持つ) */
#define ISO_RA_SECTORS        ATAPI_READ_MAX_SECTORS   /* 先読みの窓 (mount で kmalloc) */
#define ISO_RA_BYTES          ((u32)ISO_RA_SECTORS * ISO_SECTOR_SIZE)
#define ISO_IDLE_TICKS        (2 * PIT_HZ)  /* 2 秒 */
#define ISO_PATH_CACHE_MAX    VFS_MAX_PATH

typedef struct {
    u32 lba;
    u32 used;                 /* LRU の時刻 (ctx->scache_clock) */
    int valid;
    u8  data[ISO_SECTOR_SIZE];
} IsoSectorSlot;

/* 試験と調査のための数 */
typedef struct {
    u32 path_walks;           /* 根からたどった回数 (覚えたパスに当たらなかった) */
    u32 path_hits;            /* 覚えたパスに当たった回数 */
    u32 scache_hits;
    u32 scache_fills;         /* 1 セクタ読んでキャッシュへ入れた回数 */
    u32 bulk_reads;           /* 窓を通さず呼び手のバッファへまとめて読んだ回数 */
    u32 ra_fills;             /* 先読みの窓を読んだ回数 */
    u32 ra_hits;              /* 窓から写した回数 */
    u32 drops;                /* キャッシュを捨てた回数 (世代・空き時間) */
} IsoStats;

/* マウントコンテキスト */
typedef struct {
    int    dev_id;            /* ブロックデバイスインデックス */
    u32    root_lba;          /* ルートディレクトリのLBA */
    u32    root_size;         /* ルートディレクトリのサイズ (バイト) */
    u32    volume_size;       /* ボリューム総セクタ数 */
    u16    block_size;        /* 論理ブロックサイズ (通常2048) */

    char   devname[8];        /* "cd0" … (mount が組み立てた名前) */

    /* 直前に解決したパス 1 本 */
    int    pc_valid;
    char   pc_path[ISO_PATH_CACHE_MAX];
    u32    pc_lba;
    u32    pc_size;
    u8     pc_flags;

    /* 先読みの窓 (ファイルのデータ)。ra_buf が確保できなければ窓なしで動く */
    u8    *ra_buf;
    int    ra_valid;
    u32    ra_lba;
    u32    ra_count;

    /* セクタの LRU (ディレクトリと、窓なしのときの端のセクタ) */
    u32    scache_clock;
    IsoSectorSlot scache[ISO_SCACHE_SLOTS];

    /* 捨てる合図 */
    u32    media_gen;         /* 覚えたときの atapi_media_gen() */
    int    touched;           /* 一度でも媒体を読んだ */
    u32    last_tick;         /* 最後に媒体を読んだ tick */

    IsoStats stats;
} Iso9660Ctx;

/* VFS操作テーブル (外部公開) */
extern VfsOps iso9660_ops;

#endif /* ISO9660_H */
