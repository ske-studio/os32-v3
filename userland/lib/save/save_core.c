/* ======================================================================== */
/*  SAVE_CORE.C — libos32save セーブ・ロード基本処理                       */
/* ======================================================================== */

#include "libos32save.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>

/* ファイルヘッダ構造体 (24B) */
typedef struct {
    char magic[4];
    u32  version;
    u16  region_count;
    u16  _pad;
    u32  user_meta;
    u32  total_size;
} FileHeader;

/* 各領域ヘッダ構造体 (8B) */
typedef struct {
    u16  id;
    u16  _pad;
    u32  size;
} FileRegionHeader;

/* 外部参照 (save_meta.c で定義) */
extern save_migrate_fn g_migrate_cb;
extern u32             crc32_update(u32 crc, const void *data, u32 size);

/* ====================================================================== */
/*  save_begin — コンテキスト初期化                                        */
/* ====================================================================== */
void save_begin(SaveContext *c, const char magic[4], u32 version)
{
    if (c == (SaveContext *)0) return;

    memset(c, 0, sizeof(SaveContext));
    memcpy(c->magic, magic, 4);
    c->version = version;
    c->region_count = 0;
}

/* ====================================================================== */
/*  save_add_region — 保存領域の登録                                       */
/* ====================================================================== */
int save_add_region(SaveContext *c, const void *ptr, u32 size, u16 id)
{
    if (c == (SaveContext *)0 || c->region_count >= SAVE_MAX_REGIONS) {
        return -1;
    }

    c->regions[c->region_count].ptr = ptr;
    c->regions[c->region_count].size = size;
    c->regions[c->region_count].id = id;
    c->regions[c->region_count]._pad = 0;
    c->region_count++;
    return 0;
}

/* ====================================================================== */
/*  save_write — セーブファイル書き込み                                    */
/* ====================================================================== */
int save_write(SaveContext *c, const char *path, u32 user_meta)
{
    int fd;
    FileHeader fh;
    u32 total_size;
    u32 crc;
    int i;

    if (c == (SaveContext *)0 || path == (const char *)0) return -1;

    /* 新規作成・上書きオープン */
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        return -1;
    }

    /* データ部の総サイズ算出 */
    total_size = c->region_count * sizeof(FileRegionHeader);
    for (i = 0; i < (int)c->region_count; i++) {
        total_size += c->regions[i].size;
    }

    /* ヘッダ作成 */
    memcpy(fh.magic, c->magic, 4);
    fh.version = c->version;
    fh.region_count = c->region_count;
    fh._pad = 0;
    fh.user_meta = user_meta;
    fh.total_size = total_size;

    crc = 0xFFFFFFFFUL;

    /* ヘッダ書き込み */
    if (write(fd, &fh, sizeof(fh)) != sizeof(fh)) {
        close(fd);
        return -1;
    }
    crc = crc32_update(crc, &fh, sizeof(fh));

    /* 領域ヘッダ配列書き込み */
    for (i = 0; i < (int)c->region_count; i++) {
        FileRegionHeader frh;
        frh.id = c->regions[i].id;
        frh._pad = 0;
        frh.size = c->regions[i].size;

        if (write(fd, &frh, sizeof(frh)) != sizeof(frh)) {
            close(fd);
            return -1;
        }
        crc = crc32_update(crc, &frh, sizeof(frh));
    }

    /* 領域データペイロード書き込み */
    for (i = 0; i < (int)c->region_count; i++) {
        u32 size = c->regions[i].size;
        if (write(fd, c->regions[i].ptr, size) != (int)size) {
            close(fd);
            return -1;
        }
        crc = crc32_update(crc, c->regions[i].ptr, size);
    }

    /* CRC32 確定と書き込み */
    crc ^= 0xFFFFFFFFUL;
    if (write(fd, &crc, sizeof(crc)) != sizeof(crc)) {
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

/* ====================================================================== */
/*  save_read — セーブファイル読み込み・整合性検証・復元                     */
/* ====================================================================== */
int save_read(SaveContext *c, const char *path)
{
    int fd;
    FileHeader fh;
    FileRegionHeader frh_list[SAVE_MAX_REGIONS];
    u32 crc;
    int i, j;
    int migrate_needed;
    u32 file_crc;

    if (c == (SaveContext *)0 || path == (const char *)0) return -1;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    /* ヘッダ読み込み */
    if (read(fd, &fh, sizeof(fh)) != sizeof(fh)) {
        close(fd);
        return -1;
    }

    crc = 0xFFFFFFFFUL;
    crc = crc32_update(crc, &fh, sizeof(fh));

    /* 1. マジックコードの検証 */
    if (memcmp(fh.magic, c->magic, 4) != 0) {
        close(fd);
        return -2;
    }

    if (fh.region_count > SAVE_MAX_REGIONS) {
        close(fd);
        return -1; /* 不正な領域数 */
    }

    /* 領域ヘッダ配列読み込み */
    if (read(fd, frh_list, fh.region_count * sizeof(FileRegionHeader)) != (int)(fh.region_count * sizeof(FileRegionHeader))) {
        close(fd);
        return -1;
    }
    crc = crc32_update(crc, frh_list, fh.region_count * sizeof(FileRegionHeader));

    /* バージョン移行の要否判定 */
    migrate_needed = (fh.version != c->version);

    /* 領域データペイロードの読み込みと展開 */
    for (i = 0; i < (int)fh.region_count; i++) {
        u16 fid = frh_list[i].id;
        u32 fsize = frh_list[i].size;
        int target_idx = -1;

        /* コンテキストに登録されている同一IDの領域を探索 */
        for (j = 0; j < (int)c->region_count; j++) {
            if (c->regions[j].id == fid) {
                target_idx = j;
                break;
            }
        }

        if (target_idx < 0) {
            /* 登録なし領域: 読み飛ばし (CRC32計算は行う) */
            u8 *skip_buf = (u8 *)malloc(fsize);
            if (skip_buf == (u8 *)0) {
                close(fd);
                return -1;
            }
            if (read(fd, skip_buf, fsize) != (int)fsize) {
                free(skip_buf);
                close(fd);
                return -1;
            }
            crc = crc32_update(crc, skip_buf, fsize);
            free(skip_buf);
        } else {
            SaveRegion *target = &c->regions[target_idx];

            if (!migrate_needed) {
                /* 同一バージョンの場合: 直接宛先バッファへロード */
                if (target->size != fsize) {
                    close(fd);
                    return -1; /* サイズ不一致エラー */
                }
                if (read(fd, (void *)target->ptr, fsize) != (int)fsize) {
                    close(fd);
                    return -1;
                }
                crc = crc32_update(crc, target->ptr, fsize);
            } else {
                /* 異なるバージョンの場合: 移行処理 */
                u8 *old_data;
                int rc;

                if (g_migrate_cb == (save_migrate_fn)0) {
                    close(fd);
                    return -4; /* 移行コールバック未定義 */
                }

                old_data = (u8 *)malloc(fsize);
                if (old_data == (u8 *)0) {
                    close(fd);
                    return -1;
                }

                if (read(fd, old_data, fsize) != (int)fsize) {
                    free(old_data);
                    close(fd);
                    return -1;
                }
                crc = crc32_update(crc, old_data, fsize);

                /* コールバックによる新形式への変換と代入 */
                rc = g_migrate_cb(fh.version, c->version, old_data, fid, fsize, (void *)target->ptr, target->size);
                free(old_data);

                if (rc < 0) {
                    close(fd);
                    return -4; /* 移行処理失敗 */
                }
            }
        }
    }

    /* CRC32 値の読み込みと整合性検証 */
    if (read(fd, &file_crc, sizeof(file_crc)) != sizeof(file_crc)) {
        close(fd);
        return -1;
    }
    close(fd);

    crc ^= 0xFFFFFFFFUL;
    if (crc != file_crc) {
        return -3; /* チェックサム不一致 (データ破損) */
    }

    return 0;
}
