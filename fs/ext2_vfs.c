#include "ext2_priv.h"
#include "kstring.h"
#include "kmalloc.h"

/* ======== ext2 VFSラッパー ======== */
/* マルチインスタンス対応: void *ctx を Ext2Ctx* にキャストして全関数に渡す。 */
/* mount() で kmalloc 確保、umount() で kfree 解放。                        */

/* パス文字列の分離 (dir_pathとfilename) */
static void ext2_split_path(const char *path, char *dir_path, const char **filename)
{
    int last_slash = -1;
    int i;

    for (i = 0; path[i]; i++) {
        if (path[i] == '/') last_slash = i;
    }

    if (last_slash <= 0) {
        dir_path[0] = '/'; dir_path[1] = '\0';
        *filename = (path[0] == '/') ? path + 1 : path;
    } else {
        for (i = 0; i < last_slash && i < VFS_MAX_PATH - 1; i++) dir_path[i] = path[i];
        dir_path[i] = '\0';
        *filename = path + last_slash + 1;
    }
}

/* パス文字列からinode番号を解決
 *
 * VFS の FD 層 (fs/vfs_fd.c) は read_stream / write_stream のたびに
 * **パス文字列** を渡してくるので、ここを素通しにすると sys_write 1 回ごとに
 * ext2_lookup がディレクトリを先頭から辿り直す。/tmp/e8.tar への 1 バイト
 * 追記 26 セクタのうち 8 セクタがこの辿り直しだった (票 S6-P)。
 * 名前空間が動けば ns_gen が進んで記憶は全部捨てられるので、当たった記憶は
 * 必ず現物と一致する (ext2_path_memo_get / fs/ext2_ctx.h)。 */
/* 下で定義する変換。resolve_path が **VFS 番号体系で**返すために前方宣言する */
static int ext2_to_vfs_err(int rc);

/* 戻り値は **VFS 番号体系 (VFS_OK / VFS_ERR_...)**。
 * 呼び手 (13 か所) はこれをそのまま返すこと。**一律 NOTFOUND に畳まない**
 * (票 B8) — 「読めなかった」が「無い」として上がると、open の O_CREAT 経路が
 * 既存ファイルを空で作り直し、write が別ファイルを作り、mkdir/rmdir/rename が
 * 存在しないものとして振る舞う。 */
static int ext2_resolve_path(Ext2Ctx *ec, const char *path, u32 *out_ino)
{
    int rc;

    /* ルートまたは "/" */
    if (!path || !path[0] || (path[0] == '/' && !path[1])) {
        *out_ino = EXT2_ROOT_INO;
        return VFS_OK;
    }

    if (ext2_path_memo_get(ec, path, out_ino) == EXT2_OK) return VFS_OK;

    rc = ext2_lookup(ec, path, out_ino);
    if (rc == EXT2_OK) { ext2_path_memo_put(ec, path, *out_ino); return VFS_OK; }
    return ext2_to_vfs_err(rc);
}

/* ディレクトリ一覧のコールバック変換 */
typedef struct {
    vfs_dir_cb  user_cb;
    void       *user_ctx;
    Ext2Ctx    *ec;
} Ext2ListCtx;

static void ext2_to_vfs_cb(const Ext2DirEntry *e, void *ctx)
{
    Ext2ListCtx *lc = (Ext2ListCtx *)ctx;
    VfsDirEntry ve;
    int i;

    if (!e->inode) return;

    for (i = 0; i < e->name_len && i < VFS_MAX_PATH - 1; i++) ve.name[i] = e->name[i];
    ve.name[i] = '\0';

    ve.type = (e->file_type == EXT2_FT_DIR) ? VFS_TYPE_DIR : VFS_TYPE_FILE;

    /* ext2ディレクトリエントリにはサイズ情報がないためinodeから取得 */
    ve.size = 0;
    if (e->file_type != EXT2_FT_DIR) {
        ext2_get_size_ino(lc->ec, e->inode, &ve.size);
    }

    lc->user_cb(&ve, lc->user_ctx);
}

/* EXT2_ERR_* → VFS_ERR_* 変換。番号体系が異なる (EXT2 の -3 は NOTFOUND だが
 * VFS の -3 は NOMOUNT) ので、ドライバの戻り値をそのまま上げてはいけない。
 * 以前はそのまま返していたため、シェルが `rm nonexist` に -3 (NOMOUNT)、
 * `mkdir exists` に -6 (VFS では NOTDIR) を表示していた。正の値 (バイト数) は
 * そのまま通す */
static int ext2_to_vfs_err(int rc)
{
    if (rc >= 0) return rc;
    switch (rc) {
    case EXT2_ERR_IO:       return VFS_ERR_IO;
    case EXT2_ERR_MAGIC:    return VFS_ERR_IO;
    case EXT2_ERR_NOTFOUND: return VFS_ERR_NOTFOUND;
    case EXT2_ERR_NOMOUNT:  return VFS_ERR_NOMOUNT;
    case EXT2_ERR_NOSPC:    return VFS_ERR_NOSPC;
    case EXT2_ERR_EXIST:    return VFS_ERR_EXIST;
    case EXT2_ERR_NOTDIR:   return VFS_ERR_NOTDIR;
    case EXT2_ERR_NOTEMPTY: return VFS_ERR_NOTEMPTY;
    case EXT2_ERR_ISDIR:    return VFS_ERR_ISDIR;
    case EXT2_ERR_INVAL:    return VFS_ERR_INVAL;
    case EXT2_ERR_ROFS:     return VFS_ERR_ROFS;   /* 票 B8 往復 5 */
    case EXT2_ERR_MLINK:    return VFS_ERR_FULL;   /* 票 B8 往復 5 */
    default:                return VFS_ERR_IO;
    }
}

static int ext2_vfs_list(void *ctx, const char *path, vfs_dir_cb cb, void *user_ctx)
{
    Ext2Ctx *ec = (Ext2Ctx *)ctx;
    u32 ino;
    int rc;
    Ext2ListCtx lc;

    rc = ext2_resolve_path(ec, path, &ino);
    if (rc != VFS_OK) return rc;

    lc.user_cb = cb;
    lc.user_ctx = user_ctx;
    lc.ec = ec;
    return ext2_to_vfs_err(ext2_list_dir(ec, ino, ext2_to_vfs_cb, &lc));
}

static int ext2_vfs_read(void *ctx, const char *path, void *buf, u32 max_size)
{
    Ext2Ctx *ec = (Ext2Ctx *)ctx;
    u32 ino;
    int rc;
    rc = ext2_resolve_path(ec, path, &ino);
    if (rc != VFS_OK) return rc;
    return ext2_to_vfs_err(ext2_read_file(ec, ino, buf, max_size));
}

static int ext2_vfs_write(void *ctx, const char *path, const void *data, u32 size)
{
    Ext2Ctx *ec = (Ext2Ctx *)ctx;
    u32 dir_ino, file_ino;
    u8 ftype;
    int rc;
    /* パスからディレクトリ部分とファイル名を分離 */
    char dir_path[VFS_MAX_PATH];
    const char *fname;

    ext2_split_path(path, dir_path, &fname);

    rc = ext2_resolve_path(ec, dir_path, &dir_ino);
    if (rc != VFS_OK) return rc;

    /* ファイルが既存なら上書き、なければ作成。
     * **新規作成へ進むのは「本当に無い」ときだけ** (票 B8)。読めなかったのを
     * 「無い」と読み替えると、既にある名前に対して ext2_create が走り、
     * 同じ名前の二重エントリで元の inode が辿れなくなる。 */
    rc = ext2_find_entry(ec, dir_ino, fname, &file_ino, &ftype);
    if (rc == EXT2_OK) {
        /* 既存ファイル → 上書き */
        return ext2_to_vfs_err(ext2_write(ec, file_ino, data, size));
    } else if (rc == EXT2_ERR_NOTFOUND) {
        /* 新規作成 */
        return ext2_to_vfs_err(ext2_create(ec, dir_ino, fname, data, size));
    }
    return ext2_to_vfs_err(rc);
}

static int ext2_vfs_unlink(void *ctx, const char *path)
{
    Ext2Ctx *ec = (Ext2Ctx *)ctx;
    char dir_path[VFS_MAX_PATH];
    const char *fname;
    u32 dir_ino;
    int rc;

    ext2_split_path(path, dir_path, &fname);

    rc = ext2_resolve_path(ec, dir_path, &dir_ino);
    if (rc != VFS_OK) return rc;
    return ext2_to_vfs_err(ext2_unlink(ec, dir_ino, fname));
}

/* ---- 排他的作成 (票 H2 §2-1、KAPI v53 の O_EXCL) ----
 *
 * 「無いことの確認 → 作成」を **1 回の呼び出しの中で**行う。ext2_create が
 * 既に 3 値の存在確認を持っている (EXT2_OK なら EXIST、NOTFOUND のときだけ
 * 作る、それ以外はそのまま返す) ので、そこへ長さ 0 で入るだけでよい。
 * ディレクトリでも「名前が在る」= EXIST になる (find_entry は種別を見ない) —
 * O_EXCL の契約どおり ISDIR ではなく EXIST を返す。 */
static int ext2_vfs_create_excl(void *ctx, const char *path)
{
    Ext2Ctx *ec = (Ext2Ctx *)ctx;
    char dir_path[VFS_MAX_PATH];
    const char *fname;
    u32 dir_ino;
    int rc;

    if (!ec) return VFS_ERR_NOMOUNT;
    if (!path || !path[0]) return VFS_ERR_INVAL;

    ext2_split_path(path, dir_path, &fname);
    if (!fname[0]) return VFS_ERR_INVAL;

    rc = ext2_resolve_path(ec, dir_path, &dir_ino);
    if (rc != VFS_OK) return rc;
    return ext2_to_vfs_err(ext2_create(ec, dir_ino, fname, "", 0));
}

static int ext2_vfs_rename(void *ctx, const char *oldpath, const char *newpath)
{
    Ext2Ctx *ec = (Ext2Ctx *)ctx;
    char old_dir[VFS_MAX_PATH], new_dir[VFS_MAX_PATH];
    const char *old_name, *new_name;
    u32 old_ino, new_ino;
    int rc;

    ext2_split_path(oldpath, old_dir, &old_name);
    ext2_split_path(newpath, new_dir, &new_name);

    rc = ext2_resolve_path(ec, old_dir, &old_ino);
    if (rc != VFS_OK) return rc;
    rc = ext2_resolve_path(ec, new_dir, &new_ino);
    if (rc != VFS_OK) return rc;
    return ext2_to_vfs_err(ext2_rename(ec, old_ino, old_name, new_ino, new_name));
}

/* path が FS のルートそのもの ("", "/", "//" …) か */
static int ext2_path_is_root(const char *path)
{
    int i;
    if (!path) return 1;
    for (i = 0; path[i]; i++) {
        if (path[i] != '/') return 0;
    }
    return 1;
}

static int ext2_vfs_mkdir(void *ctx, const char *path)
{
    Ext2Ctx *ec = (Ext2Ctx *)ctx;
    char dir_path[VFS_MAX_PATH];
    const char *dname;
    u32 parent_ino;
    int rc;

    /* ルートは必ず在る (POSIX の mkdir("/") = EEXIST)。以前はここで
     * 親 "/" + 名前 "" に分かれ、名前の無いディレクトリを作っていた
     * (票 TASK_EXT2_EMPTY_NAME)。末尾が "/" の "/a/b/" は VFS が正規化して
     * から渡すので、ここへ来た時点で名前が空なら ext2_mkdir が INVAL で断る。 */
    if (ext2_path_is_root(path)) return VFS_ERR_EXIST;
    ext2_split_path(path, dir_path, &dname);

    rc = ext2_resolve_path(ec, dir_path, &parent_ino);
    if (rc != VFS_OK) return rc;
    return ext2_to_vfs_err(ext2_mkdir(ec, parent_ino, dname));
}

static int ext2_vfs_rmdir(void *ctx, const char *path)
{
    Ext2Ctx *ec = (Ext2Ctx *)ctx;
    char dir_path[VFS_MAX_PATH];
    const char *dname;
    u32 parent_ino;
    int rc;

    ext2_split_path(path, dir_path, &dname);

    rc = ext2_resolve_path(ec, dir_path, &parent_ino);
    if (rc != VFS_OK) return rc;
    return ext2_to_vfs_err(ext2_rmdir(ec, parent_ino, dname));
}

static int ext2_vfs_read_stream(void *ctx, const char *path, void *buf, u32 size, u32 offset)
{
    Ext2Ctx *ec = (Ext2Ctx *)ctx;
    u32 ino;
    int rc;
    rc = ext2_resolve_path(ec, path, &ino);
    if (rc != VFS_OK) return rc;
    return ext2_to_vfs_err(ext2_read_stream(ec, ino, buf, size, offset));
}

static int ext2_vfs_write_stream(void *ctx, const char *path, const void *data, u32 size, u32 offset)
{
    Ext2Ctx *ec = (Ext2Ctx *)ctx;
    u32 ino;
    int rc;
    rc = ext2_resolve_path(ec, path, &ino);
    if (rc != VFS_OK) return rc;
    return ext2_to_vfs_err(ext2_write_stream(ec, ino, data, size, offset));
}

static int ext2_vfs_get_size(void *ctx, const char *path, u32 *size)
{
    Ext2Ctx *ec = (Ext2Ctx *)ctx;
    u32 ino;
    int rc;
    rc = ext2_resolve_path(ec, path, &ino);
    if (rc != VFS_OK) return rc;
    return ext2_to_vfs_err(ext2_get_size_ino(ec, ino, size));
}

/* stat とFD の fstat (inode) で同じ値を返すための 1 か所 */
static void ext2_fill_stat(u32 ino, const Ext2Inode *inode, OS32_Stat *buf)
{
    kmemset(buf, 0, sizeof(OS32_Stat));

    buf->st_dev = 0;
    buf->st_ino = ino;
    /* ext2とOS32_StatのフラグはPOSIX互換のため直接代入可能 */
    buf->st_mode = inode->mode;
    buf->st_nlink = inode->links_count;
    buf->st_uid = inode->uid;
    buf->st_gid = inode->gid;
    buf->st_size = inode->size;
    buf->st_atime = inode->atime;
    buf->st_mtime = inode->mtime;
    buf->st_ctime = inode->ctime;
}

static int ext2_vfs_stat(void *ctx, const char *path, OS32_Stat *buf)
{
    Ext2Ctx *ec = (Ext2Ctx *)ctx;
    u32 ino;
    Ext2Inode inode;
    int rc;
    
    if (!buf) return VFS_ERR_INVAL;

    rc = ext2_resolve_path(ec, path, &ino);
    if (rc != VFS_OK) return rc;

    rc = ext2_read_inode(ec, ino, &inode);
    if (rc != 0) return VFS_ERR_IO;

    ext2_fill_stat(ino, &inode, buf);
    return VFS_OK;
}

/* ---- 更新日時の設定 (票 H3 / 設計書 §5.2) ----
 *
 * ext2 は inode に mtime を持ち、書き込みのたびに現在時刻で更新している
 * (fs/ext2_file.c)。値は元から持っていて、外から与える口が無かっただけ。
 *
 * **ctime はゲスト側の現在時刻にする** — ctime は「作成時刻」ではなく
 * inode の状態変更時刻で、いま状態を変えたのはこのゲストだから。
 * atime は触らない (同一判定に使わないし、読んだ覚えも無い)。
 *
 * mtime == 0 は「不明」の印なので受け付けない (VFS 側でも断るが、
 * FS ドライバを直接呼ぶ経路が増えても崩れないようここでも見る)。 */
static int ext2_vfs_set_mtime(void *ctx, const char *path, os_time_t mtime)
{
    Ext2Ctx *ec = (Ext2Ctx *)ctx;
    Ext2Inode inode;
    u32 ino;
    int rc;

    if (!ec) return VFS_ERR_NOMOUNT;
    if (!ext2_is_mounted_ctx(ec)) return VFS_ERR_NOMOUNT;
    if (!path) return VFS_ERR_INVAL;
    if (mtime == 0) return VFS_ERR_INVAL;
    /* 書き込み系。エラー状態なら断る (票 B8 往復 5 / 決裁 2) */
    rc = ext2_check_writable(ec);
    if (rc != 0) return ext2_to_vfs_err(rc);

    rc = ext2_resolve_path(ec, path, &ino);
    if (rc != VFS_OK) return rc;

    rc = ext2_read_inode(ec, ino, &inode);
    if (rc != 0) return ext2_to_vfs_err(rc);

    inode.mtime = (u32)mtime;
    inode.ctime = ext2_current_time();

    rc = ext2_write_inode(ec, ino, &inode);
    if (rc != 0) return ext2_to_vfs_err(rc);

    /* inode だけの変更でも媒体まで出す。ここで落ちたら「届いていない」 */
    rc = ext2_sync(ec);
    if (rc != 0) return ext2_to_vfs_err(rc);
    return VFS_OK;
}

/* ---- inode で動く口 (票 TASK_VFS_FD_PATH 方針 v3 の 1) ----
 *
 * VFS の FD は open 時にここで inode を取り、以後の read / write / fstat /
 * 切り詰めはパスを引き直さずに inode で行う。内部はもともと inode で動く
 * (ext2_read_stream / ext2_write_stream / ext2_write) ので、パスの解決を
 * 外しただけの口。通常ファイル以外は各関数が ISDIR で断る。 */
static int ext2_vfs_lookup_ino(void *ctx, const char *path, u32 *ino)
{
    Ext2Ctx *ec = (Ext2Ctx *)ctx;
    if (!ec || !ino) return VFS_ERR_INVAL;
    return ext2_resolve_path(ec, path, ino);
}

static int ext2_vfs_read_ino(void *ctx, u32 ino, void *buf, u32 size, u32 offset)
{
    return ext2_to_vfs_err(ext2_read_stream((Ext2Ctx *)ctx, ino, buf, size, offset));
}

static int ext2_vfs_write_ino(void *ctx, u32 ino, const void *buf, u32 size, u32 offset)
{
    return ext2_to_vfs_err(ext2_write_stream((Ext2Ctx *)ctx, ino, buf, size, offset));
}

static int ext2_vfs_stat_ino(void *ctx, u32 ino, OS32_Stat *buf)
{
    Ext2Inode inode;
    if (!buf) return VFS_ERR_INVAL;
    if (ext2_read_inode((Ext2Ctx *)ctx, ino, &inode) != 0) return VFS_ERR_IO;
    ext2_fill_stat(ino, &inode, buf);
    return VFS_OK;
}

/* 長さ 0 へ切り詰める (O_TRUNC)。ext2_write は通常ファイル以外を ISDIR で断る */
static int ext2_vfs_truncate_ino(void *ctx, u32 ino)
{
    return ext2_to_vfs_err(ext2_write((Ext2Ctx *)ctx, ino, "", 0));
}

static const VfsInoOps ext2_ino_ops = {
    ext2_vfs_lookup_ino, ext2_vfs_read_ino, ext2_vfs_write_ino,
    ext2_vfs_stat_ino, ext2_vfs_truncate_ino
};

/* ---- マウント/アンマウント (kmalloc/kfree) ---- */

static void *ext2_vfs_mount(int dev_id)
{
    Ext2Ctx *ec;
    /* ext2 は IDE (hd*) 専用。ext2_dev_for() / ext2_find_partition() は
     * 下位バイトだけで "hd%d" を組み立てるので、種別を確かめずに通すと
     * fd0 が hd0 として開かれ、同じパーティションが二重マウントされる。 */
    if (VFS_MOUNT_DEV_TYPE(dev_id) != VFS_DEV_HD) return (void *)0;
    ec = (Ext2Ctx *)kzalloc(sizeof(Ext2Ctx));
    if (!ec) return (void *)0;
    if (ext2_mount(ec, VFS_MOUNT_DEV_ID(dev_id)) != EXT2_OK) {
        kfree(ec);
        return (void *)0;
    }
    return (void *)ec;
}

static void ext2_vfs_umount(void *ctx)
{
    Ext2Ctx *ec = (Ext2Ctx *)ctx;
    if (ec) {
        ext2_unmount(ec);
        kfree(ec);
    }
}

static int ext2_vfs_is_mounted(void *ctx)
{
    Ext2Ctx *ec = (Ext2Ctx *)ctx;
    return ec ? ext2_is_mounted_ctx(ec) : 0;
}

static int ext2_vfs_sync(void *ctx)
{
    Ext2Ctx *ec = (Ext2Ctx *)ctx;
    return ec ? ext2_to_vfs_err(ext2_sync(ec)) : VFS_ERR_NOMOUNT;
}

static u32 ext2_vfs_total_blocks(void *ctx) {
    Ext2Ctx *ec = (Ext2Ctx *)ctx;
    return ec ? ec->sb_info.total_blocks : 0;
}
static u32 ext2_vfs_free_blocks(void *ctx) {
    Ext2Ctx *ec = (Ext2Ctx *)ctx;
    return ec ? ec->sb_info.free_blocks_count : 0;
}
static u32 ext2_vfs_block_size(void *ctx) {
    Ext2Ctx *ec = (Ext2Ctx *)ctx;
    return ec ? ec->sb_info.block_size : 0;
}

/* ext2操作テーブル */
static VfsOps ext2_ops = {
    "ext2",
    ext2_vfs_mount, ext2_vfs_umount, ext2_vfs_is_mounted,
    ext2_vfs_list, ext2_vfs_mkdir, ext2_vfs_rmdir,
    ext2_vfs_read, ext2_vfs_write, ext2_vfs_unlink,
    ext2_vfs_rename,
    ext2_vfs_get_size, ext2_vfs_read_stream, ext2_vfs_write_stream,
    ext2_vfs_sync,
    ext2_vfs_total_blocks, ext2_vfs_free_blocks, ext2_vfs_block_size,
    ext2_vfs_stat,
    ext2_vfs_set_mtime,         /* 票 H3。他の FS は埋めない = NOSYS */
    ext2_vfs_create_excl,       /* 票 H2。他の FS は埋めない = NOSYS */
    &ext2_ino_ops,              /* 票 TASK_VFS_FD_PATH。FD は inode で動く */
    0                           /* name_fold なし = 名前はバイトで区別する */
};


/* ======== 初期化・登録 ======== */
void ext2_init(void)
{
    vfs_register_fs(&ext2_ops);
}
