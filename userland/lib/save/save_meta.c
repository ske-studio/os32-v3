/* ======================================================================== */
/*  SAVE_META.C — libos32save メタ情報覗き見・ユーティリティ・CRC           */
/* ======================================================================== */

#include "libos32save.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>

#undef st_atime
#undef st_mtime
#undef st_ctime

/* ファイルヘッダ構造体 (save_core.c と同一定義) */
typedef struct {
    char magic[4];
    u32  version;
    u16  region_count;
    u16  _pad;
    u32  user_meta;
    u32  total_size;
} FileHeader;

/* 移行処理コールバックのグローバル実体 */
save_migrate_fn g_migrate_cb = (save_migrate_fn)0;

/* ====================================================================== */
/*  crc32_update — 逐次 CRC32 計算用内部ヘルパー                           */
/* ====================================================================== */
u32 crc32_update(u32 crc, const void *data, u32 size)
{
    const u8 *bytes = (const u8 *)data;
    u32 i;

    for (i = 0; i < size; i++) {
        u8 b = bytes[i];
        int j;
        crc ^= b;
        for (j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320UL;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

/* ====================================================================== */
/*  save_crc32 — 一括 CRC32 算出                                         */
/* ====================================================================== */
u32 save_crc32(const void *data, u32 size)
{
    u32 crc = crc32_update(0xFFFFFFFFUL, data, size);
    return crc ^ 0xFFFFFFFFUL;
}

/* ====================================================================== */
/*  save_set_migrate_cb — 移行処理関数の設定                              */
/* ====================================================================== */
void save_set_migrate_cb(save_migrate_fn fn)
{
    g_migrate_cb = fn;
}

/* ====================================================================== */
/*  save_peek — ヘッダのみの部分ロードとファイル日時取得                   */
/* ====================================================================== */
int save_peek(const char *path, SaveMeta *out)
{
    int fd;
    FileHeader fh;

    if (path == (const char *)0 || out == (SaveMeta *)0) {
        return -1;
    }

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    /* ヘッダのみ読み出し */
    if (read(fd, &fh, sizeof(fh)) != sizeof(fh)) {
        close(fd);
        return -1;
    }
    close(fd);

    /* メタデータ詰め込み */
    memcpy(out->magic, fh.magic, 4);
    out->version = fh.version;
    out->total_size = fh.total_size;
    out->user_meta = fh.user_meta;

    /* ファイル更新日時を st_mtime から取得 */
    {
        extern KernelAPI *kapi;
        OS32_Stat os_st;
        if (kapi != (KernelAPI *)0 && kapi->sys_stat(path, &os_st) == 0) {
            out->mtime = (u32)os_st.st_mtime;
        } else {
            out->mtime = 0;
        }
    }

    return 0;
}
