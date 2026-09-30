/* ======================================================================== */
/*  PKG.H — OS32 パッケージ (.pkg) 展開ライブラリ                            */
/*                                                                          */
/*  PKGファイルの読み込み・ヘッダ検証・LZSS解凍・ファイル展開を行う。        */
/*  cdinst.bin / fdinst.bin の両方から利用する共有コード。                   */
/* ======================================================================== */

#ifndef PKG_H
#define PKG_H

#include "os32_kapi_shared.h"

/* PKGヘッダ定数 */
#define PKG_MAGIC_0  'P'
#define PKG_MAGIC_1  'K'
#define PKG_MAGIC_2  'G'
#define PKG_MAGIC_3  '1'
#define PKG_HEADER_SIZE  32

/* フラグ */
#define PKG_FLAG_LZSS    0x01

/* エントリタイプ */
#define PKG_TYPE_FILE    0
#define PKG_TYPE_DIR     1

/* PKGヘッダ (32バイト、パディング禁止) */
typedef struct __attribute__((packed)) {
    char magic[4];      /* "PKG1" */
    char name[8];       /* パッケージ名 (NUL埋め) */
    u8   version;       /* バージョン番号 */
    u8   flags;         /* bit0=LZSS圧縮 */
    u16  kapi_ver;      /* 対応KAPIバージョン */
    u16  entry_count;   /* エントリ数 */
    u32  orig_size;     /* 元データサイズ */
    u32  comp_size;     /* 圧縮後データサイズ */
    u8   reserved[6];   /* 予約 */
} PkgHeader;

/* ファイルエントリ (パース後) */
#define PKG_MAX_PATH  128
#define PKG_MAX_ENTRIES 128

typedef struct {
    char path[PKG_MAX_PATH]; /* ゲストパス */
    u32  size;               /* ファイルサイズ */
    u8   type;               /* PKG_TYPE_FILE / PKG_TYPE_DIR */
} PkgEntry;

/* パース結果 */
typedef struct {
    PkgHeader header;
    int       entry_count;
    u32       data_offset;   /* PKGファイル内のデータ部オフセット */
    PkgEntry  entries[PKG_MAX_ENTRIES];
} PkgInfo;

/* エラーコード */
#define PKG_OK           0
#define PKG_ERR_IO      -1
#define PKG_ERR_MAGIC   -2
#define PKG_ERR_NOMEM   -3
#define PKG_ERR_CORRUPT -4
/* パスが PKG_MAX_PATH (NUL 込み) に収まらない。切り詰めずに断る
 * (票 TASK_VFS_FD_PATH 方針 v2 の 9)。展開先の前置 (/hd0) 込みで見る */
#define PKG_ERR_TOOLONG -5

/* === API === */

/* PKGヘッダとファイルテーブルを読み込んでパースする
 *   api:  KernelAPI
 *   path: PKGファイルへのパス (例: "/cd0/MINIMAL.PKG")
 *   info: パース結果を格納する構造体
 * 戻り値: PKG_OK=成功 */
int pkg_parse(KernelAPI *api, const char *path, PkgInfo *info);

/* PKGファイルの内容を展開・インストールする
 *   api:  KernelAPI
 *   path: PKGファイルへのパス
 *   info: pkg_parse()で取得したパース結果
 * 戻り値: PKG_OK=成功 */
int pkg_extract(KernelAPI *api, const char *path, const PkgInfo *info);

/* 展開先の前置 (prefix_len バイト、例: "/hd0" = 4) を付けると PKG_MAX_PATH
 * (NUL 込み) に収まらない最初の項目の番号。全部収まれば -1。
 * cdinst は展開を始める前にこれで断る (票 TASK_VFS_FD_PATH v3 の追記) */
int pkg_first_overflow(const PkgInfo *info, int prefix_len);

/* パッケージ名を取得 (NUL終端) */
void pkg_get_name(const PkgHeader *hdr, char *out, int max);

#endif /* PKG_H */
