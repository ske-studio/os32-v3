/* ======================================================================== */
/*  PKG.C — OS32 パッケージ (.pkg) 展開ライブラリ                            */
/*                                                                          */
/*  KernelAPI経由でPKGファイルを読み込み、LZSS解凍後にファイルを展開する。   */
/*  外部プログラム (cdinst/fdinst) からリンクして使用する。                  */
/* ======================================================================== */

#include "pkg.h"
#include <string.h>  /* memcpy, memset from newlib */

/* 無圧縮の展開で 1 回に読む大きさ。CD (ISO 9660) では 1 回の読みがそのまま
 * READ(10) 1〜2 回になるので大きく取る (4KB のころは実機の cdinst が 20KB/s を
 * 切った)。取れなければ PKG_STREAM_SMALL のスタックのバッファへ落ちる */
#define PKG_STREAM_CHUNK  32768U
#define PKG_STREAM_SMALL  4096U
/* 読みの区切りをこの倍数の位置に揃える (CD のセクタ)。データ部の先頭は
 * セクタに揃っていないので、揃えないと毎回の読みの両端が半端なセクタになる */
#define PKG_STREAM_ALIGN  2048U

/* LZSS定数 (lib/lzss.c と同一) */
#define LZSS_N         4096
#define LZSS_F         18
#define LZSS_THRESHOLD 2

/* LZSS展開 (lib/lzss.c のデコーダと互換)
 *   src:      圧縮データ (先頭4Bにオリジナルサイズ)
 *   src_len:  圧縮データ長
 *   dst:      展開先バッファ
 *   dst_size: 展開先バッファサイズ
 * 戻り値: 展開後のバイト数 */
static int lzss_decode_local(const u8 *src, u32 src_len,
                             u8 *dst, u32 dst_size)
{
    u8 text_buf[LZSS_N];
    u32 orig_size;
    u32 sp, dp, r;
    int flags, flag_count;

    if (src_len < 4) return 0;

    /* 先頭4バイト: オリジナルサイズ */
    orig_size = (u32)src[0] | ((u32)src[1] << 8)
              | ((u32)src[2] << 16) | ((u32)src[3] << 24);
    if (orig_size > dst_size) orig_size = dst_size;

    sp = 4;
    dp = 0;
    r  = LZSS_N - LZSS_F;
    flags = 0;
    flag_count = 0;

    memset(text_buf, 0x20, LZSS_N);

    while (dp < orig_size && sp < src_len) {
        if (flag_count == 0) {
            flags = src[sp++];
            flag_count = 8;
        }

        if (flags & 1) {
            /* リテラルバイト */
            if (sp >= src_len) break;
            dst[dp] = src[sp];
            text_buf[r] = src[sp];
            sp++;
            dp++;
            r = (r + 1) & (LZSS_N - 1);
        } else {
            /* (position, length) ペア */
            u32 pos;
            int len, k;
            if (sp + 1 >= src_len) break;
            pos = (u32)src[sp] | (((u32)src[sp + 1] & 0xF0) << 4);
            len = (src[sp + 1] & 0x0F) + LZSS_THRESHOLD + 1;
            sp += 2;
            for (k = 0; k < len && dp < orig_size; k++) {
                u8 c = text_buf[(pos + k) & (LZSS_N - 1)];
                dst[dp++] = c;
                text_buf[r] = c;
                r = (r + 1) & (LZSS_N - 1);
            }
        }

        flags >>= 1;
        flag_count--;
    }

    return (int)dp;
}

/* ======================================================================== */
/*  ヘルパー: パス中のディレクトリ部分を抽出して作成                         */
/* ======================================================================== */

static void ensure_parent_dirs(KernelAPI *api, const char *path)
{
    char buf[PKG_MAX_PATH];
    int i;

    /* パスをバッファにコピー */
    for (i = 0; i < PKG_MAX_PATH - 1 && path[i]; i++)
        buf[i] = path[i];
    buf[i] = '\0';

    /* 各 '/' 位置で区切って mkdir */
    for (i = 1; buf[i]; i++) {
        if (buf[i] == '/') {
            buf[i] = '\0';
            api->sys_mkdir(buf);
            buf[i] = '/';
        }
    }
}

/* ======================================================================== */
/*  公開API                                                                  */
/* ======================================================================== */

void pkg_get_name(const PkgHeader *hdr, char *out, int max)
{
    int i;
    for (i = 0; i < 8 && i < max - 1; i++) {
        if (hdr->name[i] == '\0') break;
        out[i] = hdr->name[i];
    }
    out[i] = '\0';
}

int pkg_parse(KernelAPI *api, const char *path, PkgInfo *info)
{
    int fd;
    int rd;
    u8 tbuf[6]; /* エントリ読み込み用一時バッファ */

    memset(info, 0, sizeof(PkgInfo));

    fd = api->sys_open(path, KAPI_O_RDONLY);
    if (fd < 0) return PKG_ERR_IO;

    /* ヘッダ読み込み (32バイト) */
    rd = api->sys_read(fd, &info->header, PKG_HEADER_SIZE);
    if (rd != PKG_HEADER_SIZE) {
        api->sys_close(fd);
        return PKG_ERR_IO;
    }

    /* マジック検証 */
    if (info->header.magic[0] != PKG_MAGIC_0 ||
        info->header.magic[1] != PKG_MAGIC_1 ||
        info->header.magic[2] != PKG_MAGIC_2 ||
        info->header.magic[3] != PKG_MAGIC_3) {
        api->sys_close(fd);
        return PKG_ERR_MAGIC;
    }

    /* ファイルテーブル読み込み。
     * **過長の項目・項目数の超過・途中で切れた表は即エラー** (票
     * TASK_VFS_FD_PATH 方針 v2 の 9)。以前は長いパスを PKG_MAX_PATH - 1 だけ
     * 読んで残りを次の項目の頭として読み (読み位置がずれ)、129 項目目以降は
     * 黙って捨て、切れた表は「そこまで」として成功を返していた。 */
    info->entry_count = 0;
    for (;;) {
        u8 path_len;
        PkgEntry *ent;

        /* パス長 (1バイト) */
        rd = api->sys_read(fd, &path_len, 1);
        if (rd != 1) { api->sys_close(fd); return PKG_ERR_CORRUPT; }

        /* 終端マーカー */
        if (path_len == 0) break;

        /* 128 項目の後に終端以外が来たら超過 */
        if (info->entry_count >= PKG_MAX_ENTRIES) {
            api->sys_close(fd);
            return PKG_ERR_CORRUPT;
        }
        /* NUL を足して収まらないパスは読まずに断る (切り詰めない) */
        if (path_len >= PKG_MAX_PATH) {
            api->sys_close(fd);
            return PKG_ERR_TOOLONG;
        }
        ent = &info->entries[info->entry_count];

        /* パス文字列 */
        rd = api->sys_read(fd, ent->path, path_len);
        if (rd != (int)path_len) { api->sys_close(fd); return PKG_ERR_CORRUPT; }
        ent->path[path_len] = '\0';

        /* サイズ (4バイト) + タイプ (1バイト) */
        rd = api->sys_read(fd, tbuf, 5);
        if (rd != 5) { api->sys_close(fd); return PKG_ERR_CORRUPT; }

        ent->size = (u32)tbuf[0] | ((u32)tbuf[1] << 8)
                  | ((u32)tbuf[2] << 16) | ((u32)tbuf[3] << 24);
        ent->type = tbuf[4];

        info->entry_count++;
    }

    api->sys_close(fd);

    /* ヘッダの項目数と表が食い違うなら壊れている */
    if ((int)info->header.entry_count != info->entry_count) return PKG_ERR_CORRUPT;

    /* データ部オフセットを計算して保存 */
    {
        u32 doff = PKG_HEADER_SIZE;
        int ei;
        for (ei = 0; ei < info->entry_count; ei++) {
            int plen = 0;
            const char *pp = info->entries[ei].path;
            while (*pp++) plen++;
            doff += 1 + plen + 5;
        }
        doff += 1; /* 終端マーカー */
        info->data_offset = doff;
    }

    return PKG_OK;
}

int pkg_first_overflow(const PkgInfo *info, int prefix_len)
{
    int i;
    for (i = 0; i < info->entry_count; i++) {
        int n = 0;
        while (info->entries[i].path[n]) n++;
        if (prefix_len + n + 1 > PKG_MAX_PATH) return i;
    }
    return -1;
}

int pkg_extract(KernelAPI *api, const char *path, const PkgInfo *info)
{
    int fd;
    u8 *comp_buf;
    u8 *data_buf;
    u32 data_offset;
    u32 comp_size, orig_size;
    int rd, i;
    int decoded;

    comp_size = info->header.comp_size;
    orig_size = info->header.orig_size;

    if (orig_size == 0) return PKG_OK; /* データなし (ディレクトリのみ) */

    /* ディレクトリを先に作成 */
    for (i = 0; i < info->entry_count; i++) {
        if (info->entries[i].type == PKG_TYPE_DIR) {
            api->sys_mkdir(info->entries[i].path);
        }
    }

    /* PKGファイルを再オープン */
    fd = api->sys_open(path, KAPI_O_RDONLY);
    if (fd < 0) return PKG_ERR_IO;

    if (!(info->header.flags & PKG_FLAG_LZSS)) {
        /* 無圧縮ストリーミング展開 (メモリ節約) */
        u8 small[PKG_STREAM_SMALL];
        u8 *tbuf = (u8 *)api->mem_alloc(PKG_STREAM_CHUNK);
        u32 cap = PKG_STREAM_CHUNK;
        u32 pos = info->data_offset;   /* ファイルの中の読み位置 */
        int result = PKG_OK;

        if (!tbuf) {
            tbuf = small;
            cap = PKG_STREAM_SMALL;
        }
        api->sys_lseek(fd, (int)info->data_offset, SEEK_SET);

        for (i = 0; i < info->entry_count; i++) {
            const PkgEntry *ent = &info->entries[i];
            int wfd;
            u32 remains = ent->size;

            if (ent->type != PKG_TYPE_FILE) continue;

            ensure_parent_dirs(api, ent->path);
            wfd = api->sys_open(ent->path, KAPI_O_WRONLY | KAPI_O_CREAT | KAPI_O_TRUNC);
            /* 失敗を飲み込まない (票 TASK_VFS_FD_PATH 方針 v2 の 9)。以前は
             * 開けなくても書けなくても PKG_OK を返し、cdinst は「OK」と出した */
            if (wfd < 0) { result = PKG_ERR_IO; break; }

            while (remains > 0) {
                /* 区切りの終わりを PKG_STREAM_ALIGN の倍数の位置に揃える */
                u32 room = cap - (pos % PKG_STREAM_ALIGN);
                u32 req = (remains > room) ? room : remains;
                int r = api->sys_read(fd, tbuf, (int)req);
                if (r <= 0) break;
                if (api->sys_write(wfd, tbuf, r) != r) break;
                remains -= (u32)r;
                pos += (u32)r;
            }
            api->sys_close(wfd);
            if (remains > 0) { result = PKG_ERR_IO; break; }
        }
        api->sys_close(fd);
        if (tbuf != small) api->mem_free(tbuf);
        return result;
    }

    /* LZSS解凍 (全体をメモリにロード) */
    data_offset = info->data_offset;
    api->sys_lseek(fd, (int)data_offset, SEEK_SET);

    /* 圧縮データ読み込み用バッファ確保 */
    comp_buf = (u8 *)api->mem_alloc(comp_size);
    if (!comp_buf) {
        api->kprintf(0x41, " [dbg: req %lu bytes] ", comp_size);
        api->sys_close(fd);
        return PKG_ERR_NOMEM;
    }

    rd = api->sys_read(fd, comp_buf, (int)comp_size);
    api->sys_close(fd);

    if (rd != (int)comp_size) {
        api->mem_free(comp_buf);
        return PKG_ERR_IO;
    }

    /* LZSS解凍 */
    data_buf = (u8 *)api->mem_alloc(orig_size);
    if (!data_buf) {
        api->mem_free(comp_buf);
        return PKG_ERR_NOMEM;
    }
    decoded = lzss_decode_local(comp_buf, comp_size, data_buf, orig_size);
    api->mem_free(comp_buf);
    if (decoded <= 0) {
        api->mem_free(data_buf);
        return PKG_ERR_CORRUPT;
    }

    /* ファイル書き出し */
    {
        u32 offset = 0;
        for (i = 0; i < info->entry_count; i++) {
            const PkgEntry *ent = &info->entries[i];
            int wfd;

            if (ent->type != PKG_TYPE_FILE) continue;
            if (offset + ent->size > (u32)decoded) {
                api->mem_free(data_buf);
                return PKG_ERR_CORRUPT;
            }

            /* 親ディレクトリ作成 */
            ensure_parent_dirs(api, ent->path);

            /* ファイル書き込み。失敗を飲み込まない */
            wfd = api->sys_open(ent->path, KAPI_O_WRONLY | KAPI_O_CREAT | KAPI_O_TRUNC);
            if (wfd < 0) {
                api->mem_free(data_buf);
                return PKG_ERR_IO;
            }
            if (api->sys_write(wfd, data_buf + offset, (int)ent->size)
                    != (int)ent->size) {
                api->sys_close(wfd);
                api->mem_free(data_buf);
                return PKG_ERR_IO;
            }
            api->sys_close(wfd);

            offset += ent->size;
        }
    }

    api->mem_free(data_buf);
    return PKG_OK;
}
