/* ======================================================================== */
/*  LIBOS32SAVE.H — OS32 セーブ状態管理ライブラリ 公開ヘッダ               */
/*                                                                          */
/*  任意のメモリ領域を Magic, Version, CRC32チェックサム付きでアトミックに   */
/*  永続化・ロード・破損検出・バージョン移行管理するライブラリ。             */
/* ======================================================================== */

#ifndef LIBOS32SAVE_H
#define LIBOS32SAVE_H

#include "os32_kapi_shared.h"    /* u8, u16, u32, i16, i32 */

#define SAVE_MAX_REGIONS  16

/* セーブ領域の登録情報 */
typedef struct {
    const void *ptr;   /* 保存対象ポインタ (読み込み時は書き込み先となる) */
    u32   size;        /* 領域のバイトサイズ */
    u16   id;          /* 領域識別ID (バージョン移行時の識別に利用) */
    u16   _pad;
} SaveRegion;

/* セーブ管理コンテキスト */
typedef struct {
    char  magic[4];                       /* 識別マジックコード (例: "DKP2") */
    u32   version;                        /* アプリケーション定義のデータバージョン */
    u16   region_count;                   /* 登録されている領域数 */
    u16   _pad;
    SaveRegion regions[SAVE_MAX_REGIONS]; /* 登録領域配列 */
} SaveContext;

/* セーブメタデータ (ヘッダ情報のみ読み込み用) */
typedef struct {
    char  magic[4];
    u32   version;
    u32   total_size;                     /* ヘッダ以降のデータ部総サイズ */
    u32   mtime;                          /* 更新時刻 (RTCのエポック秒、任意) */
    u32   user_meta;                      /* ゲーム側で定義可能なメタ情報 (ターン数など) */
} SaveMeta;

/* バージョン移行コールバック関数型
 * old_ver: ファイルのバージョン
 * new_ver: コンテキストのターゲットバージョン
 * region_data: ファイルから読み込まれた古いデータが格納されたバッファ (サイズ old_size)
 * region_id: 領域の識別ID
 * old_size: 読み込まれた古いデータのサイズ
 * dest_ptr: 新しいデータの書き込み先ポインタ (コンテキストの regions[i].ptr)
 * dest_size: コンテキストの regions[i].size (展開先のサイズ)
 * 戻り値: 0=成功, 負値=エラー
 */
typedef int (*save_migrate_fn)(u32 old_ver, u32 new_ver,
                               const void *region_data, u16 region_id, u32 old_size,
                               void *dest_ptr, u32 dest_size);

/* ====================================================================== */
/*  API — コア (save_core.c)                                              */
/* ====================================================================== */

/* セーブコンテキストの初期化開始 */
void save_begin(SaveContext *c, const char magic[4], u32 version);

/* 保存/読込対象のメモリ領域を登録
 * 戻り値: 0=成功, -1=登録上限超過
 */
int  save_add_region(SaveContext *c, const void *ptr, u32 size, u16 id);

/* ファイルへ書き込み
 * user_meta: ゲーム定義のメタ値
 * 戻り値: 0=成功, 負値=エラー (-1=I/Oエラー)
 */
int  save_write(SaveContext *c, const char *path, u32 user_meta);

/* ファイルから読み込み、整合性検証のうえ各登録領域へ書き戻す
 * 戻り値: 0=成功,
 *         -1 = I/Oエラー
 *         -2 = Magicコード不一致
 *         -3 = CRC32チェックサム不一致 (データ破損)
 *         -4 = バージョン不一致で移行不能 (コールバック未定義等)
 */
int  save_read(SaveContext *c, const char *path);

/* ====================================================================== */
/*  API — メタ・ユーティリティ (save_meta.c)                               */
/* ====================================================================== */

/* セーブファイルをロードせず、ヘッダのメタ情報のみを読み取る
 * 戻り値: 0=成功, 負値=エラー
 */
int  save_peek(const char *path, SaveMeta *out);

/* バージョン移行コールバックの設定 */
void save_set_migrate_cb(save_migrate_fn fn);

/* CRC32 チェックサム算出 */
u32  save_crc32(const void *data, u32 size);

#endif /* LIBOS32SAVE_H */
