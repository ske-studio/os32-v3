/* ======================================================================== */
/*  VFS.C — 仮想ファイルシステム ディスパッチャ                                */
/*                                                                          */
/*  各FSドライバ(ext2, fat等)への呼び出しを仲介し、                           */
/*  パス文字列ベースの統一APIを提供する。                                    */
/*                                                                          */
/*  マルチインスタンス対応: MountPointがfs_ctxを保持し、                     */
/*  各ディスパッチ関数がFSドライバにコンテキストを渡す。                     */
/* ======================================================================== */

#include "vfs.h"
#include "os32_kapi_shared.h" /* O_RDONLY, SEEK_SET 等 */
#include "kstring.h"
#include "console.h"
#include "kbd.h"

#ifndef ATTR_WHITE
#define ATTR_WHITE   TATTR_WHITE
#endif

/* ======== 内部ユーティリティ ======== */
/* 文字列関数は kstring.h (kstrcmp, kstrlen, kstrncpy) に統一 */

/* ======== カレントディレクトリ ======== */

static char cwd[VFS_MAX_PATH] = "/";

const char *vfs_cwd(void) { return cwd; }

int vfs_chdir(const char *path)
{
    char resolved[VFS_MAX_PATH];
    int kind;
    kind = vfs_resolve_path(path, resolved, VFS_MAX_PATH);
    if (kind != VFS_OK) return kind;

    /* 実在するディレクトリだけ受け付ける。以前は検証なしに cwd を書き換えて
     * いたので `cd /nonexistent` が成功して見え、以後の相対パスが全部壊れた */
    kind = vfs_path_kind(resolved);
    if (kind < 0) return kind;
    if (kind != VFS_KIND_DIR) return VFS_ERR_NOTDIR;

    kstrncpy(cwd, resolved, VFS_MAX_PATH);
    /* 末尾に / を保証 */
    {
        int len = (int)kstrlen(cwd);
        if (len > 0 && cwd[len - 1] != '/') {
            if (len < VFS_MAX_PATH - 2) { cwd[len] = '/'; cwd[len + 1] = '\0'; }
        }
    }
    return VFS_OK;
}

/* パスの正規化: 相対パス→絶対パス。
 *
 * **切り詰めずに断る** (票 TASK_VFS_FD_PATH 方針 v2 の 8 / v3 の追記)。
 * 以前は 256 バイトの作業領域へ kstrncpy / kstrncat で**切り詰めてから**
 * 正規化し、要素表 (VFS_MAX_PATH_DEPTH) から溢れた要素は黙って捨てていた。
 * `/` + `a`×254 + `X` と `…Y` が同じ名前に解決され、書き込み・削除・rename が
 * 別の対象へ作用し得た。深い `mkdir -p` は捨てた要素の手前を作り直して
 * EXIST で成功に見えた。
 *
 *   - 入力は**コピーの前に**長さを見る (NUL 抜き VFS_MAX_PATH - 1 まで)
 *   - 相対パスは cwd + "/" + 入力を大きい作業領域で正規化してから、**結果**の
 *     長さで判定する (cwd が長くても `..` で戻る入力は通る)
 *   - 要素表は積み上げの時点で見る — 32 個を保持しているところへ 33 個目を
 *     積もうとしたら断る (正規化後の数ではない。溢れた後の `..` が保持済みの
 *     要素を消して別の名前になるのを防ぐ)
 *   - 要素 1 つが 255 バイトを超えるのも断る */
#define VFS_NAME_MAX 255
/* cwd (NUL 込み VFS_MAX_PATH) + "/" + 入力 (NUL 込み VFS_MAX_PATH)。VFS は
 * 非再入 (呼び手の約束) なので static に置き、カーネルスタックを使わない。 */
static char resolve_tmp[VFS_MAX_PATH * 2 + 1];

int vfs_resolve_path(const char *input, char *output, int out_size)
{
    char *tmp = resolve_tmp;
    const char *parts[VFS_MAX_PATH_DEPTH];
    int part_len[VFS_MAX_PATH_DEPTH];
    int num_parts = 0;
    int i, p, o, in_len, t;

    /* out_size<=0 の u32 キャストは巨大長に化ける */
    if (!output || out_size <= 0) return VFS_ERR_INVAL;
    output[0] = '\0';

    if (!input || !input[0]) {
        if ((int)kstrlen(cwd) + 1 > out_size) return VFS_ERR_NAMETOOLONG;
        kstrncpy(output, cwd, (u32)out_size);
        return VFS_OK;
    }

    /* コピーの前に長さを見る。上限まで数えて NUL が無ければ断る
     * (上限の先を読まない) */
    for (in_len = 0; in_len < VFS_MAX_PATH && input[in_len]; in_len++) { }
    if (in_len >= VFS_MAX_PATH) return VFS_ERR_NAMETOOLONG;

    t = 0;
    if (input[0] != '/') {
        /* cwd + "/" + input。作業領域は両方の最大が入る大きさ */
        const char *c = cwd;
        while (*c) tmp[t++] = *c++;
        if (t > 0 && tmp[t - 1] != '/') tmp[t++] = '/';
    }
    for (i = 0; i < in_len; i++) tmp[t++] = input[i];
    tmp[t] = '\0';

    p = 0;
    while (tmp[p] != '\0') {
        int start, len;
        char c;

        if (tmp[p] == '/') { p++; continue; }

        start = p;
        while (tmp[p] != '/' && tmp[p] != '\0') p++;

        len = p - start;
        c = tmp[p];
        tmp[p] = '\0';

        if (len == 1 && tmp[start] == '.') {
            /* nop */
        } else if (len == 2 && tmp[start] == '.' && tmp[start+1] == '.') {
            if (num_parts > 0) num_parts--;
        } else {
            if (len > VFS_NAME_MAX) return VFS_ERR_NAMETOOLONG;
            /* 33 個目を積もうとした時点で断る (黙って捨てない) */
            if (num_parts >= VFS_MAX_PATH_DEPTH) return VFS_ERR_NAMETOOLONG;
            parts[num_parts] = &tmp[start];
            part_len[num_parts] = len;
            num_parts++;
        }

        if (c == '\0') break;
        p++;
    }

    /* 組み立ても切り詰めない。溢れるなら output を空にして断る */
    o = 1;
    for (i = 0; i < num_parts; i++) {
        o += part_len[i] + ((i < num_parts - 1) ? 1 : 0);
    }
    if (o + 1 > out_size) return VFS_ERR_NAMETOOLONG;

    output[0] = '/';
    o = 1;
    for (i = 0; i < num_parts; i++) {
        int j;
        for (j = 0; j < part_len[i]; j++) output[o++] = parts[i][j];
        if (i < num_parts - 1) output[o++] = '/';
    }
    output[o] = '\0';
    return VFS_OK;
}

/* 末尾の "/" を落とした後の最終要素が "." か ".." か (票 TASK_VFS_FD_PATH
 * 方針 v2 の 10)。rmdir / rename (両引数) / unlink / mkdir は正規化の**前**に
 * これを見て INVAL で断る。正規化すると `rmdir a/.` が `a` 自体を消し、
 * `mkdir a/..` が親の EXIST になる — POSIX も最終要素の `.` / `..` は断る。
 * 上限を超える長さは見ない (resolver が NAMETOOLONG で断る)。 */
static int vfs_last_is_dot(const char *path)
{
    int n, s, len;
    if (!path) return 0;
    for (n = 0; n < VFS_MAX_PATH && path[n]; n++) { }
    if (n >= VFS_MAX_PATH) return 0;
    while (n > 0 && path[n - 1] == '/') n--;
    s = n;
    while (s > 0 && path[s - 1] != '/') s--;
    len = n - s;
    return (len == 1 && path[s] == '.') ||
           (len == 2 && path[s] == '.' && path[s + 1] == '.');
}

/* ======== FSプラグイン登録 ======== */

static VfsOps *fs_registry[VFS_MAX_FS];
static int num_fs = 0;

void vfs_register_fs(VfsOps *ops)
{
    if (num_fs < VFS_MAX_FS) {
        fs_registry[num_fs++] = ops;
    }
}

/* ======== VFSグローバル状態 ======== */

/* VFS_DEV_* と VFS_MOUNT_DEV_* は fs/vfs.h が正典 (FS ドライバも参照する)。 */

typedef struct {
    int in_use;
    char prefix[VFS_MAX_PATH];
    VfsOps *ops;
    void *fs_ctx;           /* FSドライバ固有のインスタンスコンテキスト */
    int dev_type;
    int dev_id;
    char dev_name[VFS_MAX_DEVNAME];
} MountPoint;

static MountPoint mounts[VFS_MAX_FS];

/* ======== マウントの検索とパスの分離 ======== */

static MountPoint *vfs_find_mount(const char *path, const char **out_relpath)
{
    int i, match_len;
    int best_idx = -1;
    int best_len = -1;

    for (i = 0; i < VFS_MAX_FS; i++) {
        if (!mounts[i].in_use) continue;
        
        match_len = (int)kstrlen(mounts[i].prefix);
        if (match_len == 1 && mounts[i].prefix[0] == '/') {
            if (best_len < 1) { best_len = 1; best_idx = i; }
        } else {
            int j = 0;
            while (j < match_len && path[j] == mounts[i].prefix[j]) j++;
            if (j == match_len && (path[j] == '/' || path[j] == '\0')) {
                if (match_len > best_len) { best_len = match_len; best_idx = i; }
            }
        }
    }

    if (best_idx >= 0) {
        if (out_relpath) {
            *out_relpath = path + best_len;
            if (**out_relpath == '/') (*out_relpath)++; 
        }
        return &mounts[best_idx];
    }
    return (MountPoint *)0;
}

VfsOps *vfs_route(const char *path, char *rel_out, int max_rel, void **ctx_out)
{
    const char *rel_ptr;
    MountPoint *mnt;

    if (!rel_out || max_rel <= 0) return (VfsOps *)0;

    mnt = vfs_find_mount(path, &rel_ptr);
    if (!mnt) {
        if (ctx_out) *ctx_out = (void *)0;
        return (VfsOps *)0;
    }

    if (*rel_ptr == '\0') {
        kstrncpy(rel_out, "/", (u32)max_rel);
    } else {
        if (*rel_ptr != '/') {
            rel_out[0] = '/';
            kstrncpy(rel_out + 1, rel_ptr, (u32)(max_rel - 1));
        } else {
            kstrncpy(rel_out, rel_ptr, (u32)max_rel);
        }
    }
    if (ctx_out) *ctx_out = mnt->fs_ctx;
    return mnt->ops;
}

/* vfs_route が返した相対パスがマウント点そのもの ("/") か。
 * vfs_resolve_path が末尾の "/" と "//" と "." を畳むので、"/hd0"・"/hd0/"・
 * "/hd0/." はどれもここで "/" になる。 */
static int vfs_rel_is_root(const char *rel_path)
{
    return rel_path[0] == '/' && rel_path[1] == '\0';
}

/* ======== デバイス名パース ======== */

int vfs_dev_parse(const char *name, int *dev_type, int *dev_id)
{
    /* "hd0", "hd1", "fd0", "fd1" */
    if (name[0] == 'h' && name[1] == 'd' && name[2] >= '0' && name[2] <= '3') {
        *dev_type = VFS_DEV_HD;
        *dev_id = name[2] - '0';
        return VFS_OK;
    }
    if (name[0] == 'f' && name[1] == 'd' && name[2] >= '0' && name[2] <= '3') {
        *dev_type = VFS_DEV_FD;
        *dev_id = name[2] - '0';
        return VFS_OK;
    }
    /* "COM1" etc */
    if (name[0] == 'C' && name[1] == 'O' && name[2] == 'M') {
        *dev_type = VFS_DEV_SERIAL;
        *dev_id = 1;
        return VFS_OK;
    }
    /* "cd0" - "cd3" */
    if (name[0] == 'c' && name[1] == 'd' && name[2] >= '0' && name[2] <= '3') {
        *dev_type = VFS_DEV_CD;
        *dev_id = name[2] - '0';
        return VFS_OK;
    }
    /* "hostdrv" — 仮想デバイス (HostDrvFS) */
    if (name[0] == 'h' && name[1] == 'o' && name[2] == 's' && name[3] == 't' &&
        name[4] == 'd' && name[5] == 'r' && name[6] == 'v' && name[7] == '\0') {
        *dev_type = VFS_DEV_HOSTDRV;
        *dev_id = 0;
        return VFS_OK;
    }
    return VFS_ERR_INVAL;
}

/* ======== VFS 公開API ======== */

/* prefix が同じ場所か (末尾の `/` を除いて比べる。"/" 自身はそのまま) */
static int vfs_prefix_same(const char *a, const char *b)
{
    int la = (int)kstrlen(a), lb = (int)kstrlen(b), i;
    while (la > 1 && a[la - 1] == '/') la--;
    while (lb > 1 && b[lb - 1] == '/') lb--;
    if (la != lb) return 0;
    for (i = 0; i < la; i++) if (a[i] != b[i]) return 0;
    return 1;
}

int vfs_mount(const char *prefix, const char *dev_name, const char *fstype)
{
    int dev_type, dev_id, rc, i, slot;
    VfsOps *ops = (VfsOps *)0;
    void *fs_ctx;

    if (!prefix || !dev_name || !fstype) return VFS_ERR_INVAL;
    /* prefix は切り詰めて登録しない (票 TASK_VFS_FD_PATH v3 の追記)。
     * 切り詰めた prefix は別の場所にマウントされたことになる */
    for (i = 0; i < VFS_MAX_PATH && prefix[i]; i++) { }
    if (i >= VFS_MAX_PATH) return VFS_ERR_NAMETOOLONG;

    rc = vfs_dev_parse(dev_name, &dev_type, &dev_id);
    if (rc != VFS_OK) return VFS_ERR_INVAL;

    for (i = 0; i < num_fs; i++) {
        if (kstrcmp(fstype, fs_registry[i]->name) == 0) {
            ops = fs_registry[i];
            break;
        }
    }
    if (!ops) return VFS_ERR_INVAL;

    /* 同じ実デバイスを二重マウントさせない。ext2 は 1 デバイスにつき
     * 1 コンテキストしか整合しない — 2 つ目のコンテキストはマウント時の
     * スーパーブロックを抱えたまま vfs_sync() で書き戻し、1 つ目が書いた
     * 正しい空き数を巻き戻す (2026-09-10)。 */
    for (i = 0; i < VFS_MAX_FS; i++) {
        if (mounts[i].in_use && mounts[i].ops == ops &&
            mounts[i].dev_type == dev_type && mounts[i].dev_id == dev_id)
            return VFS_ERR_EXIST;
    }

    /* 同じ prefix の二重登録も断る (票 TASK_SERIAL_HOSTFS §1-v2 B-4')。
     * 以前は (ops, dev) しか見ていなかったので、`/host` に HostDrv が居る
     * ところへ別の FS を重ねられ、どちらが引かれるかは表の順で決まっていた。
     * 末尾の `/` の有無は同じ場所として比べる。 */
    for (i = 0; i < VFS_MAX_FS; i++) {
        if (mounts[i].in_use && vfs_prefix_same(mounts[i].prefix, prefix))
            return VFS_ERR_EXIST;
    }

    /* dev_type を dev_id の上位バイトにエンコードして渡す。FS ドライバは
     * VFS_MOUNT_DEV_TYPE() で自分が扱える種別かを必ず確かめること。 */
    fs_ctx = ops->mount(VFS_MOUNT_DEV_ENCODE(dev_type, dev_id));
    if (!fs_ctx) return VFS_ERR_IO;

    slot = -1;
    for (i = 0; i < VFS_MAX_FS; i++) {
        if (!mounts[i].in_use) { slot = i; break; }
    }
    if (slot < 0) {
        ops->umount(fs_ctx);
        return VFS_ERR_NOSPC;
    }

    mounts[slot].in_use = 1;
    kstrncpy(mounts[slot].prefix, prefix, VFS_MAX_PATH);
    mounts[slot].ops = ops;
    mounts[slot].fs_ctx = fs_ctx;
    mounts[slot].dev_type = dev_type;
    mounts[slot].dev_id = dev_id;
    kstrncpy(mounts[slot].dev_name, dev_name, VFS_MAX_DEVNAME);

    kstrncpy(cwd, "/", VFS_MAX_PATH);
    return VFS_OK;
}

void vfs_umount(const char *prefix)
{
    int i;
    for (i = 0; i < VFS_MAX_FS; i++) {
        if (mounts[i].in_use && kstrcmp(mounts[i].prefix, prefix) == 0) {
            /* そのマウントの FD 全部に失効の印を付けてから fs_ctx を解放する
             * (票 TASK_VFS_FD_PATH)。印を先に見るので、以後の read / write /
             * fstat / seek が解放済みの fs_ctx を触らない。 */
            vfs_fd_invalidate_mount(mounts[i].fs_ctx);
            mounts[i].ops->umount(mounts[i].fs_ctx);
            mounts[i].in_use = 0;
            mounts[i].fs_ctx = (void *)0;
            break;
        }
    }
}

int vfs_umount_checked(const char *prefix)
{
    int i, rc;

    if (!prefix) return VFS_ERR_INVAL;
    for (i = 0; i < VFS_MAX_FS; i++) {
        if (!mounts[i].in_use || kstrcmp(mounts[i].prefix, prefix) != 0)
            continue;
        /* ルートを外すと以後のパスが全部行き先を失う (シェル・ライブラリも)。 */
        if (mounts[i].prefix[0] == '/' && mounts[i].prefix[1] == '\0')
            return VFS_ERR_BUSY;
        /* 書き戻しを**先に**、失敗を拾う。vfs_umount は ops->umount の中の
         * sync の失敗を捨てるので、ここで見ないと「外せた = ディスクは正しい」
         * と言えない (N6)。失敗したら外さない — 呼び手が判断する。 */
        if (mounts[i].ops->sync) {
            rc = mounts[i].ops->sync(mounts[i].fs_ctx);
            if (rc < 0) return rc;
        }
        vfs_fd_invalidate_mount(mounts[i].fs_ctx);
        mounts[i].ops->umount(mounts[i].fs_ctx);
        mounts[i].in_use = 0;
        mounts[i].fs_ctx = (void *)0;
        return VFS_OK;
    }
    return VFS_ERR_NOTFOUND;
}

int vfs_dev_mount_count(int drive)
{
    int i, n = 0;

    if (drive < 0 || drive > 3) return VFS_ERR_INVAL;
    for (i = 0; i < VFS_MAX_FS; i++) {
        if (mounts[i].in_use && mounts[i].dev_type == VFS_DEV_HD &&
            mounts[i].dev_id == drive)
            n++;
    }
    return n;
}

int vfs_is_mounted(const char *prefix)
{
    int i;
    for (i = 0; i < VFS_MAX_FS; i++) {
        if (mounts[i].in_use && kstrcmp(mounts[i].prefix, prefix) == 0)
            return mounts[i].ops->is_mounted(mounts[i].fs_ctx);
    }
    return 0;
}

const char *vfs_fstype(const char *prefix)
{
    int i;
    for (i = 0; i < VFS_MAX_FS; i++) {
        if (mounts[i].in_use && kstrcmp(mounts[i].prefix, prefix) == 0) return mounts[i].ops->name;
    }
    return "";
}

const char *vfs_devname(const char *prefix)
{
    int i;
    for (i = 0; i < VFS_MAX_FS; i++) {
        if (mounts[i].in_use && kstrcmp(mounts[i].prefix, prefix) == 0) return mounts[i].dev_name;
    }
    return "";
}

int vfs_ls(const char *path, vfs_dir_cb cb, void *ctx)
{
    char resolved[VFS_MAX_PATH], rel_path[VFS_MAX_PATH];
    void *fs_ctx;
    VfsOps *ops;
    int rc = vfs_resolve_path(path, resolved, VFS_MAX_PATH);
    if (rc != VFS_OK) return rc;
    ops = vfs_route(resolved, rel_path, VFS_MAX_PATH, &fs_ctx);
    if (!ops || !ops->list_dir) return VFS_ERR_NOMOUNT;
    return ops->list_dir(fs_ctx, rel_path, cb, ctx);
}

int vfs_read(const char *path, void *buf, u32 max_size)
{
    char resolved[VFS_MAX_PATH], rel_path[VFS_MAX_PATH];
    void *fs_ctx;
    VfsOps *ops;
    int rc = vfs_resolve_path(path, resolved, VFS_MAX_PATH);
    if (rc != VFS_OK) return rc;
    ops = vfs_route(resolved, rel_path, VFS_MAX_PATH, &fs_ctx);
    if (!ops || !ops->read_file) return VFS_ERR_NOMOUNT;
    return ops->read_file(fs_ctx, rel_path, buf, max_size);
}

int vfs_write(const char *path, const void *data, u32 size)
{
    char resolved[VFS_MAX_PATH], rel_path[VFS_MAX_PATH];
    void *fs_ctx;
    VfsOps *ops;
    int rc = vfs_resolve_path(path, resolved, VFS_MAX_PATH);
    if (rc != VFS_OK) return rc;
    ops = vfs_route(resolved, rel_path, VFS_MAX_PATH, &fs_ctx);
    if (!ops || !ops->write_file) return VFS_ERR_NOMOUNT;
    if (vfs_rel_is_root(rel_path)) return VFS_ERR_ISDIR;
    return ops->write_file(fs_ctx, rel_path, data, size);
}

/* inode を持つ FS で、対象の inode を**操作の前に**取る (票 TASK_VFS_FD_PATH
 * 方針 v3 の 2)。*has = 1 で *ino に番号。NOTFOUND は「無い」(*has = 0、
 * VFS_OK)。それ以外の失敗はそのまま返し、呼び手は**操作をしない** — 取れない
 * まま消すと、その実体を開いている FD に失効の印を付けられない。 */
static int vfs_target_ino(VfsOps *ops, void *fs_ctx, const char *rel,
                          int *has, u32 *ino)
{
    int rc;
    *has = 0;
    *ino = 0;
    if (!ops->ino || !ops->ino->lookup) return VFS_OK;
    rc = ops->ino->lookup(fs_ctx, rel, ino);
    if (rc == VFS_ERR_NOTFOUND) return VFS_OK;
    if (rc != VFS_OK) return rc;
    *has = 1;
    return VFS_OK;
}

int vfs_rm(const char *path)
{
    char resolved[VFS_MAX_PATH], rel_path[VFS_MAX_PATH];
    void *fs_ctx;
    VfsOps *ops;
    int rc, has_ino;
    u32 ino;

    if (vfs_last_is_dot(path)) return VFS_ERR_INVAL;
    rc = vfs_resolve_path(path, resolved, VFS_MAX_PATH);
    if (rc != VFS_OK) return rc;
    ops = vfs_route(resolved, rel_path, VFS_MAX_PATH, &fs_ctx);
    if (!ops || !ops->unlink) return VFS_ERR_NOMOUNT;
    if (vfs_rel_is_root(rel_path)) return VFS_ERR_ISDIR;

    rc = vfs_target_ino(ops, fs_ctx, rel_path, &has_ino, &ino);
    if (rc != VFS_OK) return rc;
    /* 使用中の loop イメージは消させない (幾何や D88 索引を持つので、
     * 開き直しでは続けられない) */
    if (vfs_fd_pinned_busy(fs_ctx, has_ino, ino, rel_path)) return VFS_ERR_BUSY;

    rc = ops->unlink(fs_ctx, rel_path);
    /* **成否を問わず**印を付ける (票 v3 の 2 / Codex A-R2-2)。名前を消した後の
     * 失敗でも旧実体は解放されうる。余分に STALE にするほうへ倒す。 */
    if (has_ino) vfs_fd_invalidate_ino(fs_ctx, ino);
    return rc;
}

int vfs_rename(const char *oldpath, const char *newpath)
{
    char old_abs[VFS_MAX_PATH], old_rel[VFS_MAX_PATH];
    char new_abs[VFS_MAX_PATH], new_rel[VFS_MAX_PATH];
    void *old_ctx, *new_ctx;
    VfsOps *old_ops, *new_ops;
    int rc, has_old, has_new;
    u32 old_ino, new_ino;

    /* 利用中による拒否・INVAL・NAMETOOLONG は**印を付ける前**に判定する
     * (ラリー 3 の実装メモ)。印は FS の操作に入った場合だけ。 */
    if (vfs_last_is_dot(oldpath) || vfs_last_is_dot(newpath)) return VFS_ERR_INVAL;
    rc = vfs_resolve_path(oldpath, old_abs, VFS_MAX_PATH);
    if (rc != VFS_OK) return rc;
    rc = vfs_resolve_path(newpath, new_abs, VFS_MAX_PATH);
    if (rc != VFS_OK) return rc;

    old_ops = vfs_route(old_abs, old_rel, VFS_MAX_PATH, &old_ctx);
    new_ops = vfs_route(new_abs, new_rel, VFS_MAX_PATH, &new_ctx);

    if (!old_ops || !new_ops) return VFS_ERR_NOMOUNT;

    /* 異なるファイルシステム間の移動は許容しない */
    if (old_ops != new_ops || old_ctx != new_ctx) return VFS_ERR_INVAL;

    if (!old_ops->rename) return VFS_ERR_INVAL;
    /* マウント点そのものは付け替えられないし、付け替え先にもならない */
    if (vfs_rel_is_root(old_rel) || vfs_rel_is_root(new_rel)) return VFS_ERR_INVAL;

    /* ユーザー決裁 ① (ラリー 3、A-R3-1): 開いている SQLite DB とその
     * ジャーナル、およびそれらの祖先ディレクトリは付け替えさせない。SQLite は
     * ジャーナルを**開いた時の名前**で作り・消すので、付け替えるとジャーナルが
     * 別の場所に残る / 見つからない (hot journal を取り逃がす)。 */
    if (vfs_fd_rename_busy(old_ctx, old_rel, new_rel)) return VFS_ERR_BUSY;

    /* 置き換えられる宛先の inode を操作の前に取る。宛先の NOTFOUND は正常。
     * 元の inode も取る — rename(f,f) や同じ inode のハードリンク間では宛先の
     * 実体は解放されないので印を付けない。 */
    rc = vfs_target_ino(old_ops, old_ctx, old_rel, &has_old, &old_ino);
    if (rc != VFS_OK) return rc;
    rc = vfs_target_ino(old_ops, old_ctx, new_rel, &has_new, &new_ino);
    if (rc != VFS_OK) return rc;
    if (has_new && has_old && old_ino == new_ino) has_new = 0;
    /* 使用中の loop イメージを置き換えさせない。inode を持たない FS の FD は
     * パスで動くので、元の名前 (と祖先) が動くのも断る */
    if (vfs_fd_pinned_busy(old_ctx, has_new, new_ino, new_rel)) return VFS_ERR_BUSY;
    if (!old_ops->ino && vfs_fd_pinned_busy(old_ctx, 0, 0, old_rel))
        return VFS_ERR_BUSY;

    rc = old_ops->rename(old_ctx, old_rel, new_rel);
    /* 成否を問わず、置き換えられうる宛先の FD に印を付ける (公開後の失敗でも
     * 旧宛先は解放されうる、Codex A-R2-2) */
    if (has_new && has_old) vfs_fd_invalidate_ino(old_ctx, new_ino);
    return rc;
}

int vfs_mkdir(const char *path)
{
    char resolved[VFS_MAX_PATH], rel_path[VFS_MAX_PATH];
    void *fs_ctx;
    VfsOps *ops;
    int rc;
    if (vfs_last_is_dot(path)) return VFS_ERR_INVAL;
    rc = vfs_resolve_path(path, resolved, VFS_MAX_PATH);
    if (rc != VFS_OK) return rc;
    ops = vfs_route(resolved, rel_path, VFS_MAX_PATH, &fs_ctx);
    if (!ops || !ops->mkdir) return VFS_ERR_NOMOUNT;
    /* マウント点 (= FS のルート) は必ず在る。POSIX の mkdir("/") と同じ EXIST。
     * 以前は FS へ "/" のまま渡り、ext2 が名前の無いディレクトリを作っていた
     * (票 TASK_EXT2_EMPTY_NAME: pkg 展開の ensure_parent_dirs が呼ぶ
     * mkdir("/hd0") が cdinst の NHD のルートに inode 23 を作った)。
     * mkdir -p 型の呼び手 (pkg.c / tar.c) は EXIST を「在る」と読む。 */
    if (vfs_rel_is_root(rel_path)) return VFS_ERR_EXIST;   /* mkdir */
    return ops->mkdir(fs_ctx, rel_path);
}

int vfs_rmdir(const char *path)
{
    char resolved[VFS_MAX_PATH], rel_path[VFS_MAX_PATH];
    void *fs_ctx;
    VfsOps *ops;
    int rc;
    /* `rmdir a/.` を正規化すると a 自体を消す。正規化の前に断る */
    if (vfs_last_is_dot(path)) return VFS_ERR_INVAL;
    rc = vfs_resolve_path(path, resolved, VFS_MAX_PATH);
    if (rc != VFS_OK) return rc;
    ops = vfs_route(resolved, rel_path, VFS_MAX_PATH, &fs_ctx);
    if (!ops || !ops->rmdir) return VFS_ERR_NOMOUNT;
    if (vfs_rel_is_root(rel_path)) return VFS_ERR_INVAL;   /* マウント中のルート */
    /* inode を持たない FS (FAT / HostDrv) の pinned FD はパスで動く。FS の
     * rmdir がファイルを消す実装 (FatFs の f_unlink) だと使用中の実体を
     * 失うので、名前か祖先が pinned の FD に当たれば断る (二重の防御、
     * FAT 側は種別を見て NOTDIR — 実装レビュー ラリー 3)。 */
    if (!ops->ino && vfs_fd_pinned_busy(fs_ctx, 0, 0, rel_path))
        return VFS_ERR_BUSY;
    /* ディレクトリは open できないので失効させる FD は無い */
    return ops->rmdir(fs_ctx, rel_path);
}

int vfs_sync(void)
{
    int i, rc = VFS_OK, cur_rc;
    for (i = 0; i < VFS_MAX_FS; i++) {
        if (mounts[i].in_use && mounts[i].ops->sync) {
            cur_rc = mounts[i].ops->sync(mounts[i].fs_ctx);
            if (cur_rc != VFS_OK) rc = cur_rc;
        }
    }
    return rc;
}

u32 vfs_total_blocks(void) {
    MountPoint *mnt = vfs_find_mount("/", (const char **)0);
    return (mnt && mnt->ops->total_blocks) ? mnt->ops->total_blocks(mnt->fs_ctx) : 0;
}
u32 vfs_free_blocks(void) {
    MountPoint *mnt = vfs_find_mount("/", (const char **)0);
    return (mnt && mnt->ops->free_blocks) ? mnt->ops->free_blocks(mnt->fs_ctx) : 0;
}
u32 vfs_block_size(void) {
    MountPoint *mnt = vfs_find_mount("/", (const char **)0);
    return (mnt && mnt->ops->block_size) ? mnt->ops->block_size(mnt->fs_ctx) : 0;
}

/* stat の st_dev をマウント単位で決める。FS ドライバは st_dev を埋めない
 * (ext2/iso9660 は 0 固定、hostdrv/fatfs は kmemset のまま) ので、VFS が
 * 正典として上書きする。値は VFS_MOUNT_DEV_ENCODE() + 1 — +1 は「不明」を
 * 表す 0 と衝突させないため。vfs_mount() が断る二重マウントの鍵は
 * (FS, 種別, unit) で、この値は (種別, unit) だけから作るので、**同じ unit を
 * 別の FS ドライバで**マウントした 2 つ (例: 同じ hd の ext2 と FAT) は同じ
 * st_dev になる。一意なのは「同じ FS ドライバのマウント同士」の間。filer の
 * 同一判定は st_ino != 0 のときだけ st_dev/st_ino を見るので、st_ino を
 * 埋めない FAT/hostdrv とは衝突しない (独立レビュー 2026-09-10 の訂正)。
 *
 * 限界: 同じ物理ディスク上の別パーティションは区別しない。ext2 は
 * ext2_find_partition() が最初のブート可能エントリしか選ばず、同じ
 * hd0 を 2 度マウントすることもできないため、そもそも同時に見えない。 */
static u32 vfs_mount_dev_of(const MountPoint *mnt)
{
    return (u32)VFS_MOUNT_DEV_ENCODE(mnt->dev_type, mnt->dev_id) + 1U;
}

static u32 vfs_dev_of_resolved(const char *resolved)
{
    MountPoint *mnt = vfs_find_mount(resolved, (const char **)0);
    return mnt ? vfs_mount_dev_of(mnt) : 0U;
}

u32 vfs_path_dev(const char *path)
{
    char resolved[VFS_MAX_PATH];

    if (!path) return 0U;
    if (vfs_resolve_path(path, resolved, VFS_MAX_PATH) != VFS_OK) return 0U;
    return vfs_dev_of_resolved(resolved);
}

/* マウントルート ("/") の stat 合成。FatFs の f_stat はルートに
 * FR_INVALID_NAME を返すなど、FS ドライバはルートを stat できないことがある */
static void vfs_synth_root_stat(OS32_Stat *buf)
{
    kmemset(buf, 0, sizeof(OS32_Stat));
    buf->st_mode  = OS_S_IFDIR | OS_S_IRWXU;
    buf->st_nlink = 2;
}

int vfs_stat(const char *path, OS32_Stat *buf)
{
    char resolved[VFS_MAX_PATH], rel_path[VFS_MAX_PATH];
    void *fs_ctx;
    VfsOps *ops;
    u32 dev;
    int rc;

    if (!buf) return VFS_ERR_INVAL;

    rc = vfs_resolve_path(path, resolved, VFS_MAX_PATH);
    if (rc != VFS_OK) return rc;
    ops = vfs_route(resolved, rel_path, VFS_MAX_PATH, &fs_ctx);

    if (!ops) return VFS_ERR_NOMOUNT;
    dev = vfs_dev_of_resolved(resolved);
    if (!ops->stat) {
        if (vfs_rel_is_root(rel_path)) {
            vfs_synth_root_stat(buf);
            buf->st_dev = dev;
            return VFS_OK;
        }
        return VFS_ERR_NOMOUNT;
    }
    rc = ops->stat(fs_ctx, rel_path, buf);
    if (rc != VFS_OK && vfs_rel_is_root(rel_path)) {
        vfs_synth_root_stat(buf);
        buf->st_dev = dev;
        return VFS_OK;
    }
    /* FS が入れた st_dev (どれも 0) は VFS が上書きする。別デバイスで
     * inode 番号が一致しても (st_dev, st_ino) が衝突しないようにするため */
    if (rc == VFS_OK) buf->st_dev = dev;
    return rc;
}

/* ======================================================================== */
/*  vfs_set_mtime — 更新日時の設定 (票 H3 / 設計書 §5.2)                     */
/*                                                                          */
/*  VfsOps の set_mtime は**任意実装**。持たない FS は VFS_ERR_NOSYS を返す  */
/*  — これは失敗ではなく「この FS には無い」という答えで、呼び手 (hsync) は  */
/*  内容の同期を続けたまま「時刻の保存を省略した」と表示する。              */
/*                                                                          */
/*  mtime == 0 は現行 ABI で「不明」の印 (OS32_Stat に有効性ビットが無い)。  */
/*  不明を書き込めてしまうと、次の同期で「証拠が無い」状態を自分で作る       */
/*  ことになるので、ここで断る。                                            */
/* ======================================================================== */
int vfs_set_mtime(const char *path, os_time_t mtime)
{
    char resolved[VFS_MAX_PATH], rel_path[VFS_MAX_PATH];
    void *fs_ctx;
    VfsOps *ops;
    int rc;

    if (!path || !path[0]) return VFS_ERR_INVAL;
    if (mtime == 0) return VFS_ERR_INVAL;      /* 0 = 不明。書かせない */

    rc = vfs_resolve_path(path, resolved, VFS_MAX_PATH);
    if (rc != VFS_OK) return rc;
    ops = vfs_route(resolved, rel_path, VFS_MAX_PATH, &fs_ctx);
    if (!ops) return VFS_ERR_NOMOUNT;
    if (!ops->set_mtime) return VFS_ERR_NOSYS;
    return ops->set_mtime(fs_ctx, rel_path, mtime);
}

/* vfs_path_kind の list_dir プローブ用 (何もしない) */
static void vfs_kind_probe_cb(const VfsDirEntry *entry, void *ctx)
{
    (void)entry; (void)ctx;
}

int vfs_path_kind(const char *path)
{
    char resolved[VFS_MAX_PATH], rel_path[VFS_MAX_PATH];
    void *fs_ctx;
    VfsOps *ops;
    OS32_Stat st;
    int rc;

    rc = vfs_resolve_path(path, resolved, VFS_MAX_PATH);
    if (rc != VFS_OK) return rc;
    ops = vfs_route(resolved, rel_path, VFS_MAX_PATH, &fs_ctx);
    if (!ops) return VFS_ERR_NOMOUNT;

    /* マウントルートは常にディレクトリ (FS ドライバに聞かない) */
    if (vfs_rel_is_root(rel_path)) return VFS_KIND_DIR;

    /* stat を**持っている**ドライバの答えは最終判断。失敗してもプローブへ
     * 落とさない (Codex 実装レビュー 往復 3 の B6)。
     * 以前は NOTFOUND 以外の失敗を「stat 未対応」とみなして下へ落としていた
     * ので、stat が一時的に読めなかっただけのディレクトリが
     * get_file_size (ディレクトリでも成功する) でファイルに化け、
     * `cd` が NOTDIR になり `sys_open` のディレクトリ拒否をすり抜けた。
     * 「未対応」はドライバが stat を**持たない**ことで表す。 */
    if (ops->stat) {
        rc = ops->stat(fs_ctx, rel_path, &st);
        if (rc == VFS_OK) {
            return ((st.st_mode & OS_S_IFMT) == OS_S_IFDIR) ? VFS_KIND_DIR : VFS_KIND_FILE;
        }
        return rc;
    }

    /* stat を持たない FS だけのプローブ: list_dir が通ればディレクトリ、
     * get_file_size が通ればファイル。
     * **「読めなかった」エラーはそのまま伝える** — get_file_size は
     * ディレクトリでも成功するので、ここで落とすとファイルに化ける。
     * 次へ進むのは「ディレクトリではない」と分かったときだけ。 */
    if (ops->list_dir) {
        rc = ops->list_dir(fs_ctx, rel_path, vfs_kind_probe_cb, (void *)0);
        if (rc == VFS_OK) return VFS_KIND_DIR;
        if (rc != VFS_ERR_NOTDIR && rc != VFS_ERR_NOTFOUND) return rc;
    }
    if (ops->get_file_size) {
        u32 sz;
        rc = ops->get_file_size(fs_ctx, rel_path, &sz);
        if (rc == VFS_OK) return VFS_KIND_FILE;
        /* サイズ取得がディレクトリを断ったなら、それは種別が**分かった**と
         * いうこと (票 B8 の ③ で ext2 / HostDrv の両方が断るようにした)。 */
        if (rc == VFS_ERR_ISDIR) return VFS_KIND_DIR;
        /* 「読めなかった」を「無い」と読み替えない (票 B8) */
        if (rc != VFS_ERR_NOTFOUND) return rc;
    }
    return VFS_ERR_NOTFOUND;
}

/* end of vfs.c */
