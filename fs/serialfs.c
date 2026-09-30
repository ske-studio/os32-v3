/* ======================================================================== */
/*  SERIALFS.C — シリアル越しの /host の VfsOps (票 TASK_SERIAL_HOSTFS B)    */
/*                                                                          */
/*  旧 SerialFS (8896d38 で削除) の起こし直し。形式は fs/sfs_proto.h。      */
/*  VFS の契約 (§1-v2 B-3'):                                                */
/*   - stat は種別・サイズ・mtime (UNIX 秒 UTC) を返す                      */
/*   - get_file_size はディレクトリに ISDIR                                 */
/*   - list_dir はファイルに NOTDIR、エントリは境界を検査し、途中で切れた   */
/*     列挙を OK にしない。**コールバックの前に頁を手元へ写す** (コール     */
/*     バックが FS に触ると受信器の中身が書き換わる、POLICY_DEBUG §4-26)     */
/*   - read_stream は要求より多く返さず、EOF の前に 0 を返さない            */
/*   - 「無い」は NOTFOUND (ホストの status)、通信失敗は IO                  */
/*   - set_mtime / create_excl は持たない (NOSYS)                           */
/*  ホストの status は既知の OS32_ERR_* だけを通し、それ以外は IO に畳む。   */
/* ======================================================================== */
#include "serialfs.h"
#include "os32_kapi_shared.h"

static SfsClient *g_permit_cli;   /* 開いているあいだだけ非 NULL */
static int g_mounted;

void serialfs_permit(SfsClient *c) { g_permit_cli = c; }
void serialfs_forbid(void) { g_permit_cli = (SfsClient *)0; }
int  serialfs_is_mounted(void) { return g_mounted; }

/* ---- 小道具 ---- */

static void sfs_zero(void *p, u32 n)
{
    u8 *b = (u8 *)p;
    while (n--) *b++ = 0;
}

/* ホストの status を VFS の番号へ。知らない値は IO。
 * **符号は int で読む** — i32 (signed long) はホスト試験の 64 ビット環境では
 * 64 ビットになり、u32 からの変換で負の値が正に化ける。 */
static int sfs_status(int st)
{
    if (st >= 0) return st;
    switch (st) {
    case OS32_ERR_IO: case OS32_ERR_NOTFOUND: case OS32_ERR_NOSPC:
    case OS32_ERR_EXIST: case OS32_ERR_NOTDIR: case OS32_ERR_NOTEMPTY:
    case OS32_ERR_ISDIR: case OS32_ERR_INVAL: case OS32_ERR_NOSYS:
    case OS32_ERR_NAMETOOLONG: case OS32_ERR_BUSY:
    case OS32_ERR_ROFS:     /* ホストが書き込みを許していない (--allow-write の外) */
        return (int)st;
    default:
        return OS32_ERR_IO;
    }
}

static int sfs_status_of(const u8 *p)
{
    return sfs_status((int)sfs_get32(p));
}

/* 要求を出して status を返す。*body / *blen は status の後ろ */
static int sfs_rpc(SfsClient *c, u8 type, const u8 *pl, u16 len,
                   const u8 **body, u16 *blen)
{
    const u8 *resp;
    u16 rlen;
    int rc;

    if (!c) return OS32_ERR_IO;
    rc = sfs_client_call(c, type, pl, len, &resp, &rlen);
    if (rc != 0) return rc;
    *body = resp + SFS_STATUS_LEN;
    *blen = (u16)(rlen - SFS_STATUS_LEN);
    return sfs_status_of(resp);
}

/* path だけの要求 (MKDIR / RMDIR / UNLINK)。成功は 0 だけ */
static int sfs_path_op(SfsClient *c, u8 type, const char *path)
{
    u8 pl[SFS_MAX_PAYLOAD];
    const u8 *body;
    u16 blen;
    int n, rc;

    n = sfs_put_path(pl, (u16)sizeof(pl), path);
    if (n < 0) return OS32_ERR_NAMETOOLONG;
    rc = sfs_rpc(c, type, pl, (u16)n, &body, &blen);
    if (rc > 0) return OS32_ERR_IO;       /* 0 以外の正値は契約違反 */
    return rc;
}

/* ---- VfsOps ---- */

static void *sfs_mount(int dev)
{
    /* 種別を確かめる (fd0 を hd0 に化けさせない、POLICY_DEBUG §4-30)。
     * **セッションの外からは付けない** — `mount /host COM1 serialfs` を
     * 単独で打っても NULL (§1-v3)。 */
    if (VFS_MOUNT_DEV_TYPE(dev) != VFS_DEV_SERIAL) return (void *)0;
    if (!g_permit_cli || g_mounted) return (void *)0;
    g_mounted = 1;
    return g_permit_cli;
}

static void sfs_umount(void *ctx)
{
    /* 線が死んでいても手元の状態は解放する (§1-v3 (7)) */
    (void)ctx;
    g_mounted = 0;
}

static int sfs_is_mounted(void *ctx)
{
    (void)ctx;
    return g_mounted;
}

static int sfs_stat_raw(SfsClient *c, const char *path, u8 *kind,
                        u32 *size, u32 *mtime)
{
    u8 pl[SFS_PATH_MAX + 1];
    const u8 *body;
    u16 blen;
    int n, rc;

    n = sfs_put_path(pl, (u16)sizeof(pl), path);
    if (n < 0) return OS32_ERR_NAMETOOLONG;
    rc = sfs_rpc(c, SFS_T_STAT, pl, (u16)n, &body, &blen);
    if (rc < 0) return rc;
    if (rc != 0 || blen != 9) return OS32_ERR_IO;
    *kind = body[0];
    *size = sfs_get32(body + 1);
    *mtime = sfs_get32(body + 5);
    return 0;
}

static int sfs_stat(void *ctx, const char *path, OS32_Stat *buf)
{
    u8 kind = 0;
    u32 size = 0, mtime = 0;
    int rc;

    sfs_zero(buf, (u32)sizeof(*buf));
    rc = sfs_stat_raw((SfsClient *)ctx, path, &kind, &size, &mtime);
    if (rc != 0) return rc;
    if (kind == SFS_KIND_DIR) {
        buf->st_mode = (u16)(OS_S_IFDIR | 0755);
    } else if (kind == SFS_KIND_FILE) {
        buf->st_mode = (u16)(OS_S_IFREG | 0644);
    } else if (kind == SFS_KIND_OTHER) {
        /* 種別不明。ゼロの st_mode のまま返す (hsync は type_unknown) */
        buf->st_mode = 0;
    } else {
        return OS32_ERR_IO;
    }
    buf->st_nlink = 1;
    buf->st_size = size;
    buf->st_mtime = (os_time_t)mtime;
    return 0;
}

static int sfs_get_file_size(void *ctx, const char *path, u32 *size)
{
    u8 kind = 0;
    u32 sz = 0, mtime = 0;
    int rc = sfs_stat_raw((SfsClient *)ctx, path, &kind, &sz, &mtime);

    if (rc != 0) return rc;
    if (kind == SFS_KIND_DIR) return OS32_ERR_ISDIR;
    if (kind != SFS_KIND_FILE) return OS32_ERR_IO;
    *size = sz;
    return 0;
}

/* 列挙の頁の上限 (cookie が進まない相手で回り続けない) */
#define SFS_LIST_MAX_PAGES 4096

static int sfs_list_dir(void *ctx, const char *path, vfs_dir_cb cb,
                        void *user_ctx)
{
    SfsClient *c = (SfsClient *)ctx;
    u8 pl[SFS_MAX_PAYLOAD];
    u8 page[SFS_MAX_PAYLOAD];
    VfsDirEntry e;
    const u8 *body;
    u16 blen, pos;
    u32 cookie = 0, next;
    int n, rc, pages;

    for (pages = 0; pages < SFS_LIST_MAX_PAGES; pages++) {
        sfs_put32(pl, cookie);
        n = sfs_put_path(pl + 4, (u16)(sizeof(pl) - 4), path);
        if (n < 0) return OS32_ERR_NAMETOOLONG;
        rc = sfs_rpc(c, SFS_T_LIST, pl, (u16)(4 + n), &body, &blen);
        if (rc < 0) return rc;
        if (rc != 0 || blen < 4) return OS32_ERR_IO;
        next = sfs_get32(body);

        /* **コールバックの前に頁を写す。** コールバックが FS に触れば
         * 受信器 (body の指す先) は次の応答で上書きされる。 */
        for (pos = 0; pos < (u16)(blen - 4); pos++) page[pos] = body[4 + pos];
        blen = (u16)(blen - 4);

        /* まず頁全体を検査する (途中まで流してから IO にしない) */
        pos = 0;
        while (pos < blen) {
            u8 kind, nl;
            u16 k;
            if ((u32)pos + 6 > blen) return OS32_ERR_IO;
            kind = page[pos];
            nl = page[pos + 5];
            if (kind != SFS_KIND_FILE && kind != SFS_KIND_DIR &&
                kind != SFS_KIND_OTHER) return OS32_ERR_IO;
            /* 名前は 1〜255 バイト (u8) なので VfsDirEntry.name[256] に収まる */
            if (nl == 0 || (u32)pos + 6 + nl > blen) return OS32_ERR_IO;
            for (k = 0; k < nl; k++) {
                u8 ch = page[pos + 6 + k];
                if (ch == 0 || ch == '/') return OS32_ERR_IO;
            }
            if (page[pos + 6] == '.' &&
                (nl == 1 || (nl == 2 && page[pos + 7] == '.')))
                return OS32_ERR_IO;
            pos = (u16)(pos + 6 + nl);
        }

        pos = 0;
        while (pos < blen) {
            u8 nl = page[pos + 5];
            u16 k;
            for (k = 0; k < nl; k++) e.name[k] = (char)page[pos + 6 + k];
            e.name[nl] = '\0';
            e.size = sfs_get32(page + pos + 1);
            /* OTHER はファイル扱いで見せる (開けば stat が種別不明を返す) */
            e.type = (page[pos] == SFS_KIND_DIR) ? VFS_TYPE_DIR : VFS_TYPE_FILE;
            cb(&e, user_ctx);
            pos = (u16)(pos + 6 + nl);
        }

        if (next == 0) return 0;
        /* cookie は前へしか進まない。戻る・止まる相手は壊れている */
        if (next <= cookie) return OS32_ERR_IO;
        cookie = next;
    }
    return OS32_ERR_IO;       /* 打ち切り = 全部読めたとは言わない */
}

static int sfs_read_stream(void *ctx, const char *path, void *buf,
                           u32 size, u32 offset)
{
    SfsClient *c = (SfsClient *)ctx;
    u8 pl[SFS_MAX_PAYLOAD];
    u8 *dst = (u8 *)buf;
    const u8 *body;
    u16 blen, want, k;
    u32 done = 0;
    int n, rc;

    while (done < size) {
        want = (u16)((size - done > (u32)SFS_READ_MAX)
                     ? (u32)SFS_READ_MAX : (size - done));
        sfs_put32(pl, offset + done);
        sfs_put16(pl + 4, want);
        n = sfs_put_path(pl + 6, (u16)(sizeof(pl) - 6), path);
        if (n < 0) return OS32_ERR_NAMETOOLONG;
        rc = sfs_rpc(c, SFS_T_READ, pl, (u16)(6 + n), &body, &blen);
        if (rc < 0) return rc;
        /* **要求より多く返さない。** 長さ欄と本体の長さも一致させる */
        if ((u32)rc > want || (u32)rc != blen) return OS32_ERR_IO;
        for (k = 0; k < (u16)rc; k++) dst[done + k] = body[k];
        done += (u32)rc;
        if ((u16)rc < want) break;          /* 短い = EOF */
    }
    return (int)done;
}

static int sfs_read_file(void *ctx, const char *path, void *buf, u32 max)
{
    return sfs_read_stream(ctx, path, buf, max, 0);
}

/* offset から書く。first_trunc = 1 なら最初の要求で作り直す */
static int sfs_write_at(SfsClient *c, const char *path, const u8 *src,
                        u32 size, u32 offset, int first_trunc)
{
    u8 pl[SFS_MAX_PAYLOAD];
    const u8 *body;
    u16 blen, room, chunk, k;
    u32 done = 0;
    int n, rc, first = 1;

    /* 0 バイトの write_stream は何もしない (作りもしない)。write_file の
     * 0 バイトは TRUNC の要求を 1 回出して空のファイルを作る。 */
    if (size == 0 && !first_trunc) return 0;
    n = sfs_put_path(pl + 5, (u16)(sizeof(pl) - 5), path);
    if (n < 0) return OS32_ERR_NAMETOOLONG;
    room = (u16)(SFS_MAX_PAYLOAD - 5 - n);
    if (room == 0) return OS32_ERR_NAMETOOLONG;

    while (first || done < size) {
        chunk = (u16)((size - done > room) ? room : (size - done));
        sfs_put32(pl, offset + done);
        pl[4] = (u8)((first && first_trunc) ? SFS_WF_TRUNC : 0);
        for (k = 0; k < chunk; k++) pl[5 + n + k] = src[done + k];
        rc = sfs_rpc(c, SFS_T_WRITE, pl, (u16)(5 + n + chunk), &body, &blen);
        if (rc < 0) return rc;
        if ((u32)rc != chunk) return OS32_ERR_IO;   /* 書き切れない = 失敗 */
        done += chunk;
        first = 0;
    }
    return (int)done;
}

static int sfs_write_file(void *ctx, const char *path, const void *data,
                          u32 size)
{
    return sfs_write_at((SfsClient *)ctx, path, (const u8 *)data, size, 0, 1);
}

static int sfs_write_stream(void *ctx, const char *path, const void *buf,
                            u32 size, u32 offset)
{
    return sfs_write_at((SfsClient *)ctx, path, (const u8 *)buf, size,
                        offset, 0);
}

static int sfs_mkdir(void *ctx, const char *path)
{
    return sfs_path_op((SfsClient *)ctx, SFS_T_MKDIR, path);
}

static int sfs_rmdir(void *ctx, const char *path)
{
    return sfs_path_op((SfsClient *)ctx, SFS_T_RMDIR, path);
}

static int sfs_unlink(void *ctx, const char *path)
{
    return sfs_path_op((SfsClient *)ctx, SFS_T_UNLINK, path);
}

static int sfs_rename(void *ctx, const char *oldp, const char *newp)
{
    u8 pl[SFS_MAX_PAYLOAD];
    const u8 *body;
    u16 blen;
    int a, b, rc;

    a = sfs_put_path(pl, (u16)sizeof(pl), oldp);
    if (a < 0) return OS32_ERR_NAMETOOLONG;
    b = sfs_put_path(pl + a, (u16)(sizeof(pl) - a), newp);
    if (b < 0) return OS32_ERR_NAMETOOLONG;
    rc = sfs_rpc((SfsClient *)ctx, SFS_T_RENAME, pl, (u16)(a + b), &body,
                 &blen);
    if (rc > 0) return OS32_ERR_IO;
    return rc;
}

static int sfs_sync(void *ctx) { (void)ctx; return 0; }
static u32 sfs_total_blocks(void *ctx) { (void)ctx; return 0; }
static u32 sfs_free_blocks(void *ctx) { (void)ctx; return 0; }
static u32 sfs_block_size(void *ctx) { (void)ctx; return 512; }

static VfsOps g_serialfs_ops = {
    SERIALFS_NAME,
    sfs_mount,
    sfs_umount,
    sfs_is_mounted,
    sfs_list_dir,
    sfs_mkdir,
    sfs_rmdir,
    sfs_read_file,
    sfs_write_file,
    sfs_unlink,
    sfs_rename,
    sfs_get_file_size,
    sfs_read_stream,
    sfs_write_stream,
    sfs_sync,
    sfs_total_blocks,
    sfs_free_blocks,
    sfs_block_size,
    sfs_stat,
    /* set_mtime は持たない (NOSYS)。書くのはホストの日時を読む側だけ */
    0,
    /* create_excl も持たない — ホストは同時に書ける (hostdrvfs と同じ理由) */
    0,
    /* inode の口は持たない。FD はパスで動く */
    0,
    /* 名前はバイトで比べる (ホストは Ubuntu のノート = 大文字小文字を区別) */
    0
};

VfsOps *serialfs_get_ops(void)
{
    return &g_serialfs_ops;
}

void serialfs_init(void)
{
    vfs_register_fs(&g_serialfs_ops);
}
