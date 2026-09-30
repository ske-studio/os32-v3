/* ======================================================================== */
/*  ISO9660.C — ISO 9660 読み取り専用ファイルシステム VFSドライバ            */
/*                                                                          */
/*  CD-ROM上のISO 9660 Level 1 ファイルシステムをVFS経由で読み出す。         */
/*  書き込み操作は全て VFS_ERR_IO を返す (読み取り専用)。                    */
/*                                                                          */
/*  ファイル名はcase-insensitive比較 (ISO 9660は大文字格納)。               */
/*  バージョン番号 ";1" は表示・検索時に除去する。                           */
/* ======================================================================== */

#include "iso9660.h"
#include "dev.h"
#include "atapi.h"
#include "kmalloc.h"
#include "lib/kstring.h"
#include "endian_le.h"   /* LE アクセサの正典 (ここの 2 つはその別名) */

/* ======================================================================== */
/*  内部ヘルパー                                                             */
/* ======================================================================== */

extern volatile u32 tick_count;

/* 媒体から count セクタ読む (キャッシュを通らない)。読めたら時刻を覚える */
static int iso_dev_read(Iso9660Ctx *ctx, u32 lba, u32 count, void *buf)
{
    Device *dev;
    int rc;

    dev = dev_find(ctx->devname);
    if (!dev) return VFS_ERR_IO;
    rc = dev_blk_read_lba(dev, lba, (int)count, buf);
    ctx->touched = 1;
    ctx->last_tick = tick_count;
    return (rc != 0) ? VFS_ERR_IO : 0;
}

/* 覚えているもの (パス・セクタ) を全部捨てる */
static void iso_drop_caches(Iso9660Ctx *ctx)
{
    int i;
    ctx->pc_valid = 0;
    ctx->ra_valid = 0;
    for (i = 0; i < ISO_SCACHE_SLOTS; i++) ctx->scache[i].valid = 0;
    ctx->stats.drops++;
}

/* 操作の入口で呼ぶ。媒体の世代が進んだか、最後の読みから ISO_IDLE_TICKS を
 * 超えて空いたら覚えているものを捨てる。差は符号無しで取る (一周も可) */
static void iso_check_media(Iso9660Ctx *ctx)
{
    u32 gen = atapi_media_gen();
    u32 now = tick_count;

    if (gen != ctx->media_gen
        || (ctx->touched && (u32)(now - ctx->last_tick) > (u32)ISO_IDLE_TICKS)) {
        iso_drop_caches(ctx);
        ctx->touched = 0;
    }
    ctx->media_gen = gen;
}

/* 1 セクタを LRU を通して得る。戻り値はキャッシュの中のセクタ (次に
 * iso_get_sector を呼ぶまで有効)。読めなければ NULL */
static const u8 *iso_get_sector(Iso9660Ctx *ctx, u32 lba)
{
    int i;
    int victim = -1;
    IsoSectorSlot *s;

    ctx->scache_clock++;
    for (i = 0; i < ISO_SCACHE_SLOTS; i++) {
        s = &ctx->scache[i];
        if (s->valid && s->lba == lba) {
            s->used = ctx->scache_clock;
            ctx->stats.scache_hits++;
            return s->data;
        }
    }
    /* 空きか、いちばん古いものを追い出す */
    for (i = 0; i < ISO_SCACHE_SLOTS; i++) {
        s = &ctx->scache[i];
        if (!s->valid) { victim = i; break; }
        if (victim < 0 || s->used < ctx->scache[victim].used) victim = i;
    }
    s = &ctx->scache[victim];
    s->valid = 0;
    if (iso_dev_read(ctx, lba, 1, s->data) != 0) return (const u8 *)0;
    s->lba = lba;
    s->used = ctx->scache_clock;
    s->valid = 1;
    ctx->stats.scache_fills++;
    return s->data;
}

/* ファイルの [offset, offset + len) を dst へ読む (範囲は呼び手が確かめる)。
 *   file_secs: ファイルのセクタ数 (窓をファイルの外へ広げない)
 * 窓に入っている部分は窓から写す。窓より大きい、セクタに揃った範囲は dst へ
 * 直接まとめて読む。それ以外は、要るセクタから窓 1 枚ぶん (1 回の READ(10)) を
 * 先読みしてから写す。窓が無ければ端のセクタは LRU を通す。
 * 窓の先読みが落ちたら (要求の外の不良セクタかもしれない)、この呼び出しの
 * 残りは窓なしと同じ経路 = 要るセクタだけを読む。要るセクタが読めなければ失敗。 */
static int iso_read_extent(Iso9660Ctx *ctx, u32 file_lba, u32 file_secs,
                           u32 offset, u8 *dst, u32 len)
{
    u32 done = 0;
    int use_ra = (ctx->ra_buf != (u8 *)0);

    while (done < len) {
        u32 cur_off  = offset + done;
        u32 sect_idx = cur_off / ISO_SECTOR_SIZE;
        u32 sect_off = cur_off % ISO_SECTOR_SIZE;
        u32 left     = len - done;
        u32 lba      = file_lba + sect_idx;
        u32 j;

        /* 1. 窓に入っている (lba < ra_lba は符号無しの差が大きくなって外れる) */
        if (ctx->ra_valid && lba - ctx->ra_lba < ctx->ra_count) {
            u32 pos   = (lba - ctx->ra_lba) * ISO_SECTOR_SIZE + sect_off;
            u32 chunk = ctx->ra_count * ISO_SECTOR_SIZE - pos;
            if (chunk > left) chunk = left;
            for (j = 0; j < chunk; j++) dst[done + j] = ctx->ra_buf[pos + j];
            ctx->stats.ra_hits++;
            done += chunk;
            continue;
        }

        /* 2. 窓より大きい (窓なしなら 1 セクタ以上の) 揃った範囲は直接 */
        if (sect_off == 0
            && left >= (use_ra ? ISO_RA_BYTES : (u32)ISO_SECTOR_SIZE)) {
            u32 n = left / ISO_SECTOR_SIZE;
            if (iso_dev_read(ctx, lba, n, dst + done) != 0)
                return VFS_ERR_IO;
            ctx->stats.bulk_reads++;
            done += n * ISO_SECTOR_SIZE;
            continue;
        }

        /* 3. 要るセクタから窓 1 枚ぶんを先読み (次の周で 1. が写す) */
        if (use_ra) {
            u32 n;
            /* 起きない (範囲は呼び手が見る) が、0 本の窓で回り続けないように */
            if (sect_idx >= file_secs) return VFS_ERR_IO;
            n = file_secs - sect_idx;
            if (n > ISO_RA_SECTORS) n = ISO_RA_SECTORS;
            ctx->ra_valid = 0;
            if (iso_dev_read(ctx, lba, n, ctx->ra_buf) != 0) {
                /* 窓は無効のまま。要るセクタだけを読み直す (2. / 4.) */
                use_ra = 0;
                continue;
            }
            ctx->ra_lba   = lba;
            ctx->ra_count = n;
            ctx->ra_valid = 1;
            ctx->stats.ra_fills++;
            continue;
        }

        /* 4. 窓なし: 端のセクタを LRU から */
        {
            const u8 *sec = iso_get_sector(ctx, lba);
            u32 chunk = ISO_SECTOR_SIZE - sect_off;
            if (!sec) return VFS_ERR_IO;
            if (chunk > left) chunk = left;
            for (j = 0; j < chunk; j++) dst[done + j] = sec[sect_off + j];
            done += chunk;
        }
    }
    return 0;
}

/* 大文字変換 */
static char iso_toupper(char c)
{
    if (c >= 'a' && c <= 'z') return c - 32;
    return c;
}

/* VFS の名前比較 (BUSY / pinned) 用の 1 バイト版。iso_toupper と同じ規則 */
static u8 iso_name_fold(u8 c)
{
    return (u8)iso_toupper((char)c);
}

/* case-insensitive文字列比較 */
static int iso_strcasecmp(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = iso_toupper(*a);
        char cb = iso_toupper(*b);
        if (ca != cb) return (int)ca - (int)cb;
        a++;
        b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

/* PVD/ディレクトリレコードからリトルエンディアンu32読み出し (both-endian) */
static u32 iso_read_le32(const u8 *p)
{
    return le32_rd(p);
}

/* PVD/ディレクトリレコードからリトルエンディアンu16読み出し */
static u16 iso_read_le16(const u8 *p)
{
    return le16_rd(p);
}

/* ファイル名からバージョン番号 ";1" を除去し、末尾の "." も除去
 *   入力: "README.TXT;1" → 出力: "README.TXT"
 *   入力: "MYDIR"        → 出力: "MYDIR"      (ディレクトリ)
 */
static void iso_clean_name(const u8 *raw, int raw_len, char *out, int out_max)
{
    int i;
    int len = raw_len;

    /* バージョン番号 ";N" を除去 */
    for (i = 0; i < len; i++) {
        if (raw[i] == ';') {
            len = i;
            break;
        }
    }
    /* 末尾の "." を除去 (ディレクトリ名) */
    if (len > 0 && raw[len - 1] == '.') len--;

    if (len >= out_max) len = out_max - 1;
    for (i = 0; i < len; i++) out[i] = (char)raw[i];
    out[len] = '\0';
}

/* ======================================================================== */
/*  ディレクトリ走査コア                                                      */
/*                                                                          */
/*  パスを "/" で分解し、各コンポーネントをディレクトリ内で検索。             */
/*  最終コンポーネントのエクステント位置とサイズを返す。                     */
/* ======================================================================== */

/* ディレクトリエクステント内で名前を検索
 *   dir_lba:  ディレクトリのLBA
 *   dir_size: ディレクトリのサイズ (バイト)
 *   name:     検索するファイル/ディレクトリ名
 *   out_lba:  見つかったエントリのエクステントLBA
 *   out_size: 見つかったエントリのデータサイズ
 *   out_flags: ファイルフラグ
 * 戻り値: 0=発見, VFS_ERR_NOTFOUND=未発見 */
static int iso_find_in_dir(Iso9660Ctx *ctx, u32 dir_lba, u32 dir_size,
                           const char *name,
                           u32 *out_lba, u32 *out_size, u8 *out_flags)
{
    const u8 *sector = (const u8 *)0;
    u32 offset = 0;

    while (offset < dir_size) {
        u32 sect_lba = dir_lba + (offset / ISO_SECTOR_SIZE);
        u32 sect_off = offset % ISO_SECTOR_SIZE;
        const u8 *rec;
        u8 rec_len;
        u8 name_len;
        char entry_name[ISO_MAX_NAME];

        if (sect_off == 0 || !sector) {
            sector = iso_get_sector(ctx, sect_lba);
            if (!sector) return VFS_ERR_IO;
        }

        rec = sector + sect_off;
        rec_len = rec[0];

        /* レコード長0 = セクタ末尾のパディング → 次のセクタへ */
        if (rec_len == 0) {
            offset = ((offset / ISO_SECTOR_SIZE) + 1) * ISO_SECTOR_SIZE;
            continue;
        }

        /* レコードがセクタ境界をまたぐ場合は次のセクタで再読み込み */
        if (sect_off + rec_len > ISO_SECTOR_SIZE) {
            offset = ((offset / ISO_SECTOR_SIZE) + 1) * ISO_SECTOR_SIZE;
            continue;
        }

        name_len = rec[32];

        /* "." (0x00) と ".." (0x01) はスキップ */
        if (name_len == 1 && (rec[33] == 0x00 || rec[33] == 0x01)) {
            offset += rec_len;
            continue;
        }

        iso_clean_name(rec + 33, name_len, entry_name, ISO_MAX_NAME);

        if (iso_strcasecmp(entry_name, name) == 0) {
            *out_lba   = iso_read_le32(rec + 2);
            *out_size  = iso_read_le32(rec + 10);
            *out_flags = rec[25];
            return 0;
        }

        offset += rec_len;
    }

    return VFS_ERR_NOTFOUND;
}

/* パスを辿ってエントリを解決
 *   path: "/" 区切りのパス (例: "DIR1/SUBDIR/FILE.TXT")
 *   out_lba, out_size, out_flags: 最終エントリの情報
 * 戻り値: 0=成功 */
static int iso_resolve_path(Iso9660Ctx *ctx, const char *path,
                            u32 *out_lba, u32 *out_size, u8 *out_flags)
{
    u32 cur_lba  = ctx->root_lba;
    u32 cur_size = ctx->root_size;
    u8 cur_flags = ISO_FLAG_DIRECTORY;
    const char *p = path;
    char component[ISO_MAX_NAME];
    int ci;

    /* 直前に解決したパスなら引き直さない */
    if (ctx->pc_valid && kstrcmp(ctx->pc_path, path) == 0) {
        ctx->stats.path_hits++;
        *out_lba   = ctx->pc_lba;
        *out_size  = ctx->pc_size;
        *out_flags = ctx->pc_flags;
        return 0;
    }
    ctx->stats.path_walks++;

    /* 先頭の "/" をスキップ */
    while (*p == '/') p++;

    /* 空パス = ルートディレクトリ */
    if (*p == '\0') {
        *out_lba   = cur_lba;
        *out_size  = cur_size;
        *out_flags = cur_flags;
        return 0;
    }

    while (*p) {
        int ret;

        /* 次のコンポーネント抽出 */
        ci = 0;
        while (*p && *p != '/' && ci < ISO_MAX_NAME - 1) {
            component[ci++] = *p++;
        }
        component[ci] = '\0';
        while (*p == '/') p++;

        /* 現在のディレクトリがディレクトリでなければエラー */
        if (!(cur_flags & ISO_FLAG_DIRECTORY))
            return VFS_ERR_NOTDIR;

        ret = iso_find_in_dir(ctx, cur_lba, cur_size, component,
                              &cur_lba, &cur_size, &cur_flags);
        if (ret != 0) return ret;
    }

    /* 覚える (入り切らない長さのパスは覚えない) */
    if (kstrlen(path) < ISO_PATH_CACHE_MAX) {
        kstrncpy(ctx->pc_path, path, ISO_PATH_CACHE_MAX);
        ctx->pc_lba   = cur_lba;
        ctx->pc_size  = cur_size;
        ctx->pc_flags = cur_flags;
        ctx->pc_valid = 1;
    }

    *out_lba   = cur_lba;
    *out_size  = cur_size;
    *out_flags = cur_flags;
    return 0;
}

/* ======================================================================== */
/*  VFS操作関数                                                              */
/* ======================================================================== */

/* --- mount --- */
static void *iso9660_mount(int dev_id)
{
    u8 pvd[ISO_SECTOR_SIZE];
    Iso9660Ctx *ctx;
    Device *dev;
    char devname[8];

    /* iso9660 は CD 専用。種別を確かめずに下位バイトだけで "cd%d" を
     * 組み立てると、fd0/hd0 が cd0 として開かれる。 */
    if (VFS_MOUNT_DEV_TYPE(dev_id) != VFS_DEV_CD) return (void *)0;

    /* デバイス名を構築: "cd0", "cd1", ... */
    devname[0] = 'c';
    devname[1] = 'd';
    devname[2] = '0' + (char)VFS_MOUNT_DEV_ID(dev_id);
    devname[3] = '\0';

    dev = dev_find(devname);
    if (!dev) return (void *)0;

    /* PVD読み出し (LBA 16) */
    if (dev_blk_read_lba(dev, ISO_PVD_LBA, 1, pvd) != 0)
        return (void *)0;

    /* マジック確認: pvd[1..5] == "CD001" */
    if (pvd[0] != 1 || pvd[1] != 'C' || pvd[2] != 'D'
                     || pvd[3] != '0' || pvd[4] != '0' || pvd[5] != '1')
        return (void *)0;

    /* kzalloc: 未初期化フィールドが残らないようにする */
    ctx = (Iso9660Ctx *)kzalloc(sizeof(Iso9660Ctx));
    if (!ctx) return (void *)0;

    ctx->dev_id = dev_id;
    kstrncpy(ctx->devname, devname, sizeof(ctx->devname));
    ctx->media_gen = atapi_media_gen();
    /* 先読みの窓。取れなければ窓なしで動く (遅いだけ) */
    ctx->ra_buf = (u8 *)kmalloc(ISO_RA_BYTES);

    /* ボリュームサイズ (pvd[80..83] = LE u32) */
    ctx->volume_size = iso_read_le32(pvd + 80);

    /* 論理ブロックサイズ (pvd[128..129] = LE u16) */
    ctx->block_size = iso_read_le16(pvd + 128);
    if (ctx->block_size == 0) ctx->block_size = ISO_SECTOR_SIZE;

    /* ルートディレクトリレコード (pvd[156..189], 34バイト) */
    {
        u8 *root_rec = pvd + 156;
        ctx->root_lba  = iso_read_le32(root_rec + 2);
        ctx->root_size = iso_read_le32(root_rec + 10);
    }

    return (void *)ctx;
}

/* --- umount --- */
static void iso9660_umount(void *ctx_raw)
{
    Iso9660Ctx *ctx = (Iso9660Ctx *)ctx_raw;
    if (!ctx) return;
    if (ctx->ra_buf) kfree(ctx->ra_buf);
    kfree(ctx);
}

/* --- is_mounted --- */
static int iso9660_is_mounted(void *ctx_raw)
{
    return (ctx_raw != (void *)0) ? 1 : 0;
}

/* 入口で媒体を確かめてからパスを解決する。解決のあいだに媒体の世代が進んだら
 * (旧媒体のキャッシュで得た LBA を新媒体で読んだかもしれない)、捨てて 1 回だけ
 * 解決し直す。2 回とも進んだら失敗。*gen_out には解決を始めた世代を返す。 */
static int iso_resolve_checked(Iso9660Ctx *ctx, const char *path,
                               u32 *out_lba, u32 *out_size, u8 *out_flags,
                               u32 *gen_out)
{
    int attempt;

    for (attempt = 0; attempt < 2; attempt++) {
        u32 gen0;
        int ret;

        iso_check_media(ctx);
        gen0 = ctx->media_gen;
        ret = iso_resolve_path(ctx, path, out_lba, out_size, out_flags);
        if (atapi_media_gen() == gen0) {
            if (gen_out) *gen_out = gen0;
            return ret;
        }
    }
    return VFS_ERR_IO;
}

/* --- list_dir --- */
static int iso9660_list_dir(void *ctx_raw, const char *path,
                            vfs_dir_cb cb, void *user_ctx)
{
    Iso9660Ctx *ctx = (Iso9660Ctx *)ctx_raw;
    u32 dir_lba, dir_size;
    u8 dir_flags;
    u8 sector[ISO_SECTOR_SIZE];
    const u8 *cur = sector;
    u32 offset;
    u32 gen0 = 0;
    int ret;

    if (!ctx) return VFS_ERR_NOMOUNT;

    ret = iso_resolve_checked(ctx, path, &dir_lba, &dir_size, &dir_flags, &gen0);
    if (ret != 0) return ret;
    if (!(dir_flags & ISO_FLAG_DIRECTORY)) return VFS_ERR_NOTDIR;

    offset = 0;
    while (offset < dir_size) {
        u32 sect_lba = dir_lba + (offset / ISO_SECTOR_SIZE);
        u32 sect_off = offset % ISO_SECTOR_SIZE;
        const u8 *rec;
        u8 rec_len, name_len;

        /* キャッシュから手元へ写す。cb がこの FS を読み直すと (§4-26)
         * キャッシュの中身が入れ替わるので、指したまま cb を呼ばない */
        if (sect_off == 0) {
            const u8 *sec = iso_get_sector(ctx, sect_lba);
            if (!sec) return VFS_ERR_IO;
            /* 途中で媒体が替わった: 続きは別の媒体。一覧は中断する
             * (cb に渡した分は取り消せないので読み直さない) */
            if (atapi_media_gen() != gen0) return VFS_ERR_IO;
            kmemcpy(sector, sec, ISO_SECTOR_SIZE);
            cur = sector;
        }

        rec = cur + sect_off;
        rec_len = rec[0];

        if (rec_len == 0) {
            offset = ((offset / ISO_SECTOR_SIZE) + 1) * ISO_SECTOR_SIZE;
            continue;
        }
        if (sect_off + rec_len > ISO_SECTOR_SIZE) {
            offset = ((offset / ISO_SECTOR_SIZE) + 1) * ISO_SECTOR_SIZE;
            continue;
        }

        name_len = rec[32];

        /* "." / ".." はスキップ */
        if (!(name_len == 1 && (rec[33] == 0x00 || rec[33] == 0x01))) {
            VfsDirEntry ent;
            iso_clean_name(rec + 33, name_len, ent.name, VFS_MAX_PATH);
            ent.size = iso_read_le32(rec + 10);
            ent.type = (rec[25] & ISO_FLAG_DIRECTORY)
                     ? VFS_TYPE_DIR : VFS_TYPE_FILE;
            cb(&ent, user_ctx);
            /* cb がこの FS を読んで媒体の交換を踏んだかもしれない */
            if (atapi_media_gen() != gen0) return VFS_ERR_IO;
        }

        offset += rec_len;
    }

    return VFS_OK;
}

/* path の [offset, offset + size) を buf へ読む (read_file / read_stream の本体)。
 * 読んでいるあいだに媒体の世代が進んだら (UNIT ATTENTION を atapi が出し直しで
 * 吸った)、覚えていたパスとセクタは古い媒体のものかもしれないので、捨てて
 * 1 回だけ読み直す。2 回とも進んだら失敗にする。 */
static int iso_read_range(Iso9660Ctx *ctx, const char *path,
                          void *buf, u32 size, u32 offset)
{
    int attempt;

    for (attempt = 0; attempt < 2; attempt++) {
        u32 file_lba, file_size, gen0;
        u32 to_read = 0;
        u8 flags;
        int ret;

        iso_check_media(ctx);
        gen0 = ctx->media_gen;

        ret = iso_resolve_path(ctx, path, &file_lba, &file_size, &flags);
        if (ret == 0 && (flags & ISO_FLAG_DIRECTORY)) ret = VFS_ERR_ISDIR;
        if (ret == 0) {
            if (offset < file_size) {
                u32 secs = (u32)(((file_size - 1) / ISO_SECTOR_SIZE) + 1);
                to_read = file_size - offset;
                if (to_read > size) to_read = size;
                ret = iso_read_extent(ctx, file_lba, secs, offset,
                                      (u8 *)buf, to_read);
            }
        }
        if (atapi_media_gen() == gen0) {
            return (ret != 0) ? ret : (int)to_read;
        }
    }
    return VFS_ERR_IO;
}

/* --- read_file --- */
static int iso9660_read_file(void *ctx_raw, const char *path,
                             void *buf, u32 max_size)
{
    Iso9660Ctx *ctx = (Iso9660Ctx *)ctx_raw;

    if (!ctx) return VFS_ERR_NOMOUNT;
    return iso_read_range(ctx, path, buf, max_size, 0);
}

/* --- read_stream (オフセット付き部分読み出し) --- */
static int iso9660_read_stream(void *ctx_raw, const char *path,
                               void *buf, u32 size, u32 offset)
{
    Iso9660Ctx *ctx = (Iso9660Ctx *)ctx_raw;

    if (!ctx) return VFS_ERR_NOMOUNT;
    return iso_read_range(ctx, path, buf, size, offset);
}

/* --- get_file_size --- */
static int iso9660_get_file_size(void *ctx_raw, const char *path, u32 *size)
{
    Iso9660Ctx *ctx = (Iso9660Ctx *)ctx_raw;
    u32 lba, fsize;
    u8 flags;
    int ret;

    if (!ctx) return VFS_ERR_NOMOUNT;
    ret = iso_resolve_checked(ctx, path, &lba, &fsize, &flags, (u32 *)0);
    if (ret != 0) return ret;
    if (size) *size = fsize;
    return VFS_OK;
}

/* --- stat --- */
static int iso9660_stat(void *ctx_raw, const char *path, OS32_Stat *st)
{
    Iso9660Ctx *ctx = (Iso9660Ctx *)ctx_raw;
    u32 lba, fsize;
    u8 flags;
    int ret;

    if (!ctx) return VFS_ERR_NOMOUNT;
    ret = iso_resolve_checked(ctx, path, &lba, &fsize, &flags, (u32 *)0);
    if (ret != 0) return ret;

    if (st) {
        st->st_size = fsize;
        st->st_mode = (flags & ISO_FLAG_DIRECTORY)
                    ? (OS_S_IFDIR | OS_S_IRWXU)
                    : (OS_S_IFREG | OS_S_IRUSR);
        st->st_mtime = 0;
        st->st_atime = 0;
        st->st_ctime = 0;
        st->st_dev   = 0;
        st->st_ino   = lba;
        st->st_nlink = 1;
        st->st_uid   = 0;
        st->st_gid   = 0;
    }
    return VFS_OK;
}

/* --- 読み取り専用: 書き込み系は全て拒否 --- */
static int iso9660_write_file(void *c, const char *p, const void *d, u32 s)
{ (void)c; (void)p; (void)d; (void)s; return VFS_ERR_IO; }

static int iso9660_unlink(void *c, const char *p)
{ (void)c; (void)p; return VFS_ERR_IO; }

static int iso9660_rename(void *c, const char *o, const char *n)
{ (void)c; (void)o; (void)n; return VFS_ERR_IO; }

static int iso9660_mkdir(void *c, const char *p)
{ (void)c; (void)p; return VFS_ERR_IO; }

static int iso9660_rmdir(void *c, const char *p)
{ (void)c; (void)p; return VFS_ERR_IO; }

static int iso9660_write_stream(void *c, const char *p, const void *b,
                                u32 s, u32 o)
{ (void)c; (void)p; (void)b; (void)s; (void)o; return VFS_ERR_IO; }

static int iso9660_sync(void *c) { (void)c; return VFS_OK; }

/* --- FS情報 --- */
static u32 iso9660_total_blocks(void *c)
{
    Iso9660Ctx *ctx = (Iso9660Ctx *)c;
    return ctx ? ctx->volume_size : 0;
}

static u32 iso9660_free_blocks(void *c)
{
    (void)c;
    return 0; /* 読み取り専用 → 空きなし */
}

static u32 iso9660_block_size_fn(void *c)
{
    Iso9660Ctx *ctx = (Iso9660Ctx *)c;
    return ctx ? (u32)ctx->block_size : ISO_SECTOR_SIZE;
}

/* ======================================================================== */
/*  VfsOps テーブル                                                          */
/* ======================================================================== */

VfsOps iso9660_ops = {
    "iso9660",
    iso9660_mount, iso9660_umount, iso9660_is_mounted,
    iso9660_list_dir, iso9660_mkdir, iso9660_rmdir,
    iso9660_read_file, iso9660_write_file, iso9660_unlink,
    iso9660_rename,
    iso9660_get_file_size, iso9660_read_stream, iso9660_write_stream,
    iso9660_sync,
    iso9660_total_blocks, iso9660_free_blocks, iso9660_block_size_fn,
    iso9660_stat,
    /* set_mtime は持たない (票 H3)。vfs_set_mtime が OS32_ERR_NOSYS を
     * 返す = 失敗ではなく「この FS には無い」。**明示的に 0 を置く** —
     * -Wmissing-field-initializers が「書き忘れ」と区別できないため。 */
    0,
    /* create_excl (O_EXCL) も持たない (票 H2 §2-1)。**ホスト側が同時に書ける
     * FS では排他性が成り立たない**ので、持てないものは持たないと言う。
     * vfs_open が O_EXCL に OS32_ERR_NOSYS を返し、呼び手 (hsync) は
     * 直接上書きへ落ちずに replace_unsupported で断る。 */
    0,
    /* inode で動く口も持たない (票 TASK_VFS_FD_PATH)。FD はパスで動く */
    0,
    /* iso_strcasecmp と同じ規則 (英字だけ区別しない) (BUSY / pinned の名前比較用) */
    iso_name_fold
};
