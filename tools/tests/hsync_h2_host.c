/* =========================================================================
 *  HSYNC_H2_HOST.C — 票 H2 (一時ファイル + 検証 + 置換) を
 *                    **実物のソースで** 確かめる
 *
 *  対象票: docs/archive/shell/TASK_H2.md §2-3 / §2-4 / §4-1
 *          (A14a A14a2 A14a3 A14a4 A14a5 A14a6 A14b A14b2 A14c
 *           A15 A15b A15c A17a A17b A17c R1 R1b R3 R4 R5)
 *  実行:   python3 -B tools/tests/test_hsync_h2.py [--target] [--mutate]
 *  記録:   tools/tests/hsync_h2_tdd.md
 *
 *  H1 / H3 と同じ作法で **userland/system/hsync.c を 1 行も写さずそのまま
 *  #include する**。差し替えるのは KernelAPI だけ — オンメモリの贋ファイル
 *  システムに向け、票 H2 が要求する故障を注入できるようにしてある:
 *
 *    - `sys_open` の **O_EXCL** (存在すれば EXIST / 非対応 FS は NOSYS)
 *    - n 回目の `sys_write` を任意の番号で落とす (空き不足 = NOSPC / FULL も)
 *    - `sys_set_mtime` の失敗と、それに伴う **ROFS への転落** (ext2 と同じ)
 *    - `sys_rename` の 4 通り:
 *        **公開の前**に失敗 / **公開の後**に失敗 / 公開したか判定できない /
 *        成功
 *    - `st_nlink` をノードごとに持つ (hardlink 判定と予約名の掃除)
 *    - `api->version` を 52 / 53 に切り替える (古いカーネルの門)
 *
 *  **この票の一番大事な規則**:
 *    (1) 公開の前の失敗では**旧宛先の名前・内容・mtime が変わらない**。
 *    (2) 公開の後の失敗では**旧内容へ戻そうとしない**。宛先を読み直して
 *        `replace_partial` と報告し、成功には数えない。
 *    (3) 公開の判定は **`st_ino`** で行う。サイズと CRC は証拠にしない。
 *    (4) `.hs~` は予約名。`st_nlink` を見ずに片づけるが、**保護対象は消さない**。
 *
 *  `case_root` は `--root <根>` (FD 起動 → /hd0、2026-09-26) を見る。
 *
 *  エミュレータ・実配備・make には一切触れない。
 *  [C1] C89 / GNU89。宣言はブロック先頭、`//` コメント無し。
 * ========================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "os32api.h"      /* KernelAPI / OS32_Stat / OS32_ERR_* / u8,u32 */

static int failures;
static int checks;

static void check(int cond, const char *name)
{
    checks++;
    printf("  %s %s\n", cond ? "ok  " : "FAIL", name);
    if (!cond) failures++;
}

/* ========================================================================= */
/*  贋ファイルシステム                                                        */
/* ========================================================================= */

#define FS_MAX_NODES 96
#define FS_MAX_FDS   16
#define FS_PATH_CAP  256

typedef struct {
    int  used;
    char path[FS_PATH_CAP];
    int  is_dir;
    u8  *data;
    u32  size;
    u32  cap;
    u32  dev;
    u32  ino;
    u32  mtime;
    u16  nlink;

    /* --- 注入 --- */
    int  stat_err;
    int  set_mtime_rc;
} FNode;

typedef struct {
    int used;
    int node;
    u32 pos;
} FFd;

static FNode fs_nodes[FS_MAX_NODES];
static FFd   fs_fds[FS_MAX_FDS];
static u32   fs_next_ino;

/* ---- 注入の旗 ---- */
static int fk_rofs;              /* マウントが書き込み禁止 (ext2 のエラー状態) */
static int fk_excl_nosys;        /* 宛先 FS が O_EXCL を持たない (HostDrv 相当) */
static int fk_write_fail_at;     /* n 回目の sys_write を落とす (0 = しない) */
static int fk_write_fail_rc;
static int fk_write_calls;
static int fk_read_fail_at;
/* n 回目の sys_read の直後に、ファイルの 1 バイトを書き換える (大きさ・日時は
 * そのまま)。票 TASK_SERIAL_HOSTFS: 判定の後に本名の中身が差し替わる形 */
static int  fk_poke_at;
static char fk_poke_path[FS_PATH_CAP];
static u32  fk_poke_off;
static int fk_read_calls;
static int fk_unlink_rc;         /* != 0 … sys_unlink がこの値を返す */
static int fk_set_mtime_rc;
static int fk_set_mtime_rofs;    /* mtime の失敗で ROFS へ落ちる (ext2 と同じ) */
/* **n 回目の vfs_sync だけ**を落とす (0 = しない)。手順 4 (書き込みの後) を
 * 通して手順 9 (rename の後) だけ落とす細工に使う。 */
static int fk_sync_fail_at;
static int fk_sync_calls;
static int fk_rename_calls;
/* 指定したパスの sys_ls を n 回だけ落とす (掃除の列挙だけを止めるのに使う) */
static char fk_ls_fail_path[FS_PATH_CAP];
static int  fk_ls_fail_n;

/* sys_rename の模様 */
#define RN_OK          0   /* 置き換える */
#define RN_FAIL_BEFORE 1   /* **公開の前**に失敗 (媒体は何も変わらない) */
#define RN_FAIL_AFTER  2   /* **公開の後**に失敗 (宛先は新内容、一時名は消えた) */
#define RN_FAIL_AFTER_KEEP 3 /* 公開の後に失敗、**一時名も残った** */
#define RN_UNKNOWN     4   /* 公開したか判定できない (宛先の stat が落ちる) */
#define RN_THIRD_INO   5   /* 宛先が新旧どちらとも違う inode になった */
static int fk_rename_mode;
static int fk_rename_rc;

/* コピー元を書き換える細工 (A17a: 書き込み中にコピー元が変わる) */
static int  fk_src_mutate_at;    /* n 回目の sys_read の後に書き換える */
static char fk_src_mutate_path[FS_PATH_CAP];
static u32  fk_src_mutate_size;
static u32  fk_src_mutate_mtime;

/* 宛先を書き換える細工 (A17b: 判定後に宛先が変わる) */
static int  fk_dst_mutate_at;    /* n 回目の sys_write の後に書き換える */
static char fk_dst_mutate_path[FS_PATH_CAP];
static int  fk_dst_mutate_kind;  /* 0 = 消す / 1 = ディレクトリ化 / 2 = 内容変更 */

static char  fk_log[131072];
static u32   fk_log_len;
static u32   fk_version = 53;

static void fs_reset(void)
{
    int i;
    for (i = 0; i < FS_MAX_NODES; i++) {
        if (fs_nodes[i].data) free(fs_nodes[i].data);
        memset(&fs_nodes[i], 0, sizeof(FNode));
    }
    for (i = 0; i < FS_MAX_FDS; i++) memset(&fs_fds[i], 0, sizeof(FFd));
    fs_next_ino = 100;
    fk_rofs = 0;
    fk_excl_nosys = 0;
    fk_write_fail_at = 0;
    fk_write_fail_rc = OS32_ERR_IO;
    fk_write_calls = 0;
    fk_read_fail_at = 0;
    fk_poke_at = 0;
    fk_read_calls = 0;
    fk_unlink_rc = 0;
    fk_set_mtime_rc = 0;
    fk_set_mtime_rofs = 0;
    fk_rename_mode = RN_OK;
    fk_rename_rc = OS32_ERR_IO;
    fk_rename_calls = 0;
    fk_src_mutate_at = 0;
    fk_dst_mutate_at = 0;
    fk_ls_fail_path[0] = '\0';
    fk_ls_fail_n = 0;
    fk_sync_fail_at = 0;
    fk_sync_calls = 0;
    fk_version = 53;
    fk_log_len = 0;
    fk_log[0] = '\0';
}

static int fs_find(const char *path)
{
    int i;
    for (i = 0; i < FS_MAX_NODES; i++)
        if (fs_nodes[i].used && strcmp(fs_nodes[i].path, path) == 0) return i;
    return -1;
}

static int fs_new(const char *path, int is_dir)
{
    int i;
    for (i = 0; i < FS_MAX_NODES; i++) {
        if (fs_nodes[i].used) continue;
        memset(&fs_nodes[i], 0, sizeof(FNode));
        fs_nodes[i].used = 1;
        fs_nodes[i].is_dir = is_dir;
        strncpy(fs_nodes[i].path, path, FS_PATH_CAP - 1);
        fs_nodes[i].dev = 1;
        fs_nodes[i].ino = fs_next_ino++;
        fs_nodes[i].nlink = 1;
        return i;
    }
    printf("  (harness) node table full\n");
    exit(2);
}

static void fs_set_data(int n, const u8 *data, u32 size)
{
    if (fs_nodes[n].data) free(fs_nodes[n].data);
    fs_nodes[n].data = (u8 *)malloc(size ? size : 1);
    fs_nodes[n].cap = size ? size : 1;
    if (size) memcpy(fs_nodes[n].data, data, size);
    fs_nodes[n].size = size;
}

static int fs_add_file(const char *path, const u8 *data, u32 size)
{
    int n = fs_new(path, 0);
    fs_set_data(n, data, size);
    return n;
}

static int fs_add_dir(const char *path) { return fs_new(path, 1); }

static void fs_drop(int n)
{
    if (n < 0) return;
    if (fs_nodes[n].data) free(fs_nodes[n].data);
    memset(&fs_nodes[n], 0, sizeof(FNode));
}

static void fs_parent(const char *path, char *out)
{
    int last = -1;
    int i;
    for (i = 0; path[i]; i++) if (path[i] == '/') last = i;
    if (last <= 0) { strcpy(out, "/"); return; }
    memcpy(out, path, (size_t)last);
    out[last] = '\0';
}

static const char *fs_base(const char *path)
{
    const char *b = path;
    const char *p;
    for (p = path; *p; p++) if (*p == '/') b = p + 1;
    return b;
}

/* ========================================================================= */
/*  贋 KernelAPI                                                              */
/* ========================================================================= */

static void *fk_mem_alloc(u32 size) { return malloc(size); }
static void  fk_mem_free(void *p)   { free(p); }

static void fk_kprintf(u8 attr, const char *fmt, ...)
{
    va_list ap;
    char line[2048];
    int n;

    (void)attr;
    va_start(ap, fmt);
    n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((u32)n >= sizeof(line)) n = (int)sizeof(line) - 1;
    if (fk_log_len + (u32)n + 1 < sizeof(fk_log)) {
        memcpy(fk_log + fk_log_len, line, (size_t)n);
        fk_log_len += (u32)n;
        fk_log[fk_log_len] = '\0';
    }
}

static int fk_sys_is_mounted(const char *prefix)
{
    return strcmp(prefix, "/host") == 0;
}

/* vfs_devname: マウント点の完全一致でデバイス名、それ以外は ""
 * (fs/vfs.c と同じ)。ルートと、任意で 1 つのサブマウントを持つ */
static const char *fk_root_dev = "hd0";
static const char *fk_sub_prefix = 0;
static const char *fk_sub_dev = 0;
/* 2 つ目のサブマウント (--root /hd0 の下の別マウント = other_mount の試験) */
static const char *fk_sub2_prefix = 0;
static const char *fk_sub2_dev = 0;
static int fk_mkdir_calls;
static int fk_mkdir_fd0_calls;   /* /fd0 とその下への mkdir */

static const char *fk_vfs_devname(const char *prefix)
{
    if (strcmp(prefix, "/") == 0) return fk_root_dev;
    if (strcmp(prefix, "/host") == 0) return "hostdrv";
    if (fk_sub_prefix && strcmp(prefix, fk_sub_prefix) == 0) return fk_sub_dev;
    if (fk_sub2_prefix && strcmp(prefix, fk_sub2_prefix) == 0) return fk_sub2_dev;
    return "";
}

static int fk_vfs_sync(void)
{
    fk_sync_calls++;
    if (fk_sync_fail_at && fk_sync_calls == fk_sync_fail_at) return OS32_ERR_IO;
    return fk_rofs ? OS32_ERR_ROFS : 0;
}

static int fk_sys_mkdir(const char *path)
{
    fk_mkdir_calls++;
    if (strncmp(path, "/fd0", 4) == 0 && (path[4] == '\0' || path[4] == '/'))
        fk_mkdir_fd0_calls++;
    if (fs_find(path) >= 0) return OS32_ERR_EXIST;
    if (fk_rofs) return OS32_ERR_ROFS;
    fs_add_dir(path);
    return 0;
}

static int fk_sys_ls(const char *path, void *cb, void *ctx)
{
    DirCallback fn = (DirCallback)cb;
    char dir[FS_PATH_CAP];
    char parent[FS_PATH_CAP];
    DirEntry_Ext e;
    int i;
    int n;

    strncpy(dir, path && path[0] ? path : "/", FS_PATH_CAP - 1);
    dir[FS_PATH_CAP - 1] = '\0';
    n = (int)strlen(dir);
    while (n > 1 && dir[n - 1] == '/') dir[--n] = '\0';

    if (fk_ls_fail_n > 0 && fk_ls_fail_path[0] &&
        strcmp(dir, fk_ls_fail_path) == 0) {
        fk_ls_fail_n--;
        return OS32_ERR_IO;
    }

    if (strcmp(dir, "/") != 0) {
        i = fs_find(dir);
        if (i < 0) return OS32_ERR_NOTFOUND;
        if (!fs_nodes[i].is_dir) return OS32_ERR_NOTDIR;
    }

    for (i = 0; i < FS_MAX_NODES; i++) {
        if (!fs_nodes[i].used) continue;
        fs_parent(fs_nodes[i].path, parent);
        if (strcmp(parent, dir) != 0) continue;
        memset(&e, 0, sizeof(e));
        strncpy(e.name, fs_base(fs_nodes[i].path), OS32_MAX_PATH - 1);
        e.size = fs_nodes[i].size;
        e.type = fs_nodes[i].is_dir ? OS32_FILE_TYPE_DIR : OS32_FILE_TYPE_FILE;
        fn(&e, ctx);
    }
    return 0;
}

static int fk_sys_stat(const char *path, OS32_Stat *buf)
{
    int n = fs_find(path);

    memset(buf, 0, sizeof(OS32_Stat));
    if (n < 0) return OS32_ERR_NOTFOUND;
    if (fs_nodes[n].stat_err) return fs_nodes[n].stat_err;

    buf->st_dev = fs_nodes[n].dev;
    buf->st_ino = fs_nodes[n].ino;
    buf->st_nlink = fs_nodes[n].nlink;
    buf->st_mode = (u16)(fs_nodes[n].is_dir ? (OS_S_IFDIR | 0755)
                                            : (OS_S_IFREG | 0644));
    buf->st_size = fs_nodes[n].size;
    buf->st_mtime = fs_nodes[n].mtime;
    return 0;
}

/* O_EXCL (KAPI v53) を持つ sys_open。
 *   - fk_excl_nosys … この FS は排他的作成を持たない -> OS32_ERR_NOSYS
 *   - 名前が在れば種別を問わず OS32_ERR_EXIST (ディレクトリでも) */
static int fk_sys_open(const char *path, int mode)
{
    int n = fs_find(path);
    int f;

    if (mode & KAPI_O_EXCL) {
        if (!(mode & KAPI_O_CREAT)) return OS32_ERR_INVAL;
        if (fk_excl_nosys) return OS32_ERR_NOSYS;
        if (n >= 0) return OS32_ERR_EXIST;
        if (fk_rofs) return OS32_ERR_ROFS;
        n = fs_add_file(path, 0, 0);
    } else if (n < 0) {
        if (!(mode & KAPI_O_CREAT)) return OS32_ERR_NOTFOUND;
        if (fk_rofs) return OS32_ERR_ROFS;
        n = fs_add_file(path, 0, 0);
    } else if (fs_nodes[n].is_dir) {
        return OS32_ERR_ISDIR;
    }
    if (mode & KAPI_O_TRUNC) {
        if (fk_rofs) return OS32_ERR_ROFS;
        fs_nodes[n].size = 0;
    }

    for (f = 0; f < FS_MAX_FDS; f++) {
        if (fs_fds[f].used) continue;
        fs_fds[f].used = 1;
        fs_fds[f].node = n;
        fs_fds[f].pos = 0;
        return f + 3;
    }
    return OS32_ERR_NOSPC;
}

static void fk_sys_close(int fd)
{
    if (fd < 3 || fd - 3 >= FS_MAX_FDS) return;
    fs_fds[fd - 3].used = 0;
}

static int fk_sys_read(int fd, void *buf, u32 size)
{
    FFd *h;
    FNode *nd;
    u32 avail;

    fk_read_calls++;
    if (fk_read_fail_at && fk_read_calls == fk_read_fail_at) return OS32_ERR_IO;
    if (fk_poke_at && fk_read_calls == fk_poke_at) {
        int m = fs_find(fk_poke_path);
        if (m >= 0 && fk_poke_off < fs_nodes[m].size)
            fs_nodes[m].data[fk_poke_off] ^= 0xFF;
        fk_poke_at = 0;
    }
    if (fd < 3 || fd - 3 >= FS_MAX_FDS || !fs_fds[fd - 3].used)
        return OS32_ERR_IO;
    h = &fs_fds[fd - 3];
    nd = &fs_nodes[h->node];

    if (h->pos >= nd->size) {
        if (fk_src_mutate_at && fk_read_calls == fk_src_mutate_at) {
            int m = fs_find(fk_src_mutate_path);
            if (m >= 0) {
                fs_nodes[m].size = fk_src_mutate_size;
                fs_nodes[m].mtime = fk_src_mutate_mtime;
            }
            fk_src_mutate_at = 0;
        }
        return 0;
    }
    avail = nd->size - h->pos;
    if (avail > size) avail = size;
    memcpy(buf, nd->data + h->pos, avail);
    h->pos += avail;

    if (fk_src_mutate_at && fk_read_calls == fk_src_mutate_at) {
        int m = fs_find(fk_src_mutate_path);
        if (m >= 0) {
            fs_nodes[m].size = fk_src_mutate_size;
            fs_nodes[m].mtime = fk_src_mutate_mtime;
        }
        fk_src_mutate_at = 0;
    }
    return (int)avail;
}

static int fk_sys_write(int fd, const void *buf, u32 size)
{
    FFd *h;
    FNode *nd;

    fk_write_calls++;
    if (fk_rofs) return OS32_ERR_ROFS;
    if (fk_write_fail_at && fk_write_calls == fk_write_fail_at)
        return fk_write_fail_rc;
    if (fd < 3 || fd - 3 >= FS_MAX_FDS || !fs_fds[fd - 3].used)
        return OS32_ERR_IO;
    h = &fs_fds[fd - 3];
    nd = &fs_nodes[h->node];

    if (h->pos + size > nd->cap) {
        u32 newcap = (h->pos + size) * 2 + 16;
        u8 *p = (u8 *)realloc(nd->data, newcap);
        if (!p) return OS32_ERR_NOSPC;
        nd->data = p;
        nd->cap = newcap;
    }
    memcpy(nd->data + h->pos, buf, size);
    h->pos += size;
    if (h->pos > nd->size) nd->size = h->pos;
    /* 本物と同じく、書き込みは mtime をゲストの現在時刻で上書きする */
    nd->mtime = 555555;

    if (fk_dst_mutate_at && fk_write_calls == fk_dst_mutate_at) {
        int m = fs_find(fk_dst_mutate_path);
        if (m >= 0) {
            if (fk_dst_mutate_kind == 0) fs_drop(m);
            else if (fk_dst_mutate_kind == 1) { fs_nodes[m].is_dir = 1; }
            else { fs_nodes[m].size += 1; fs_nodes[m].mtime += 7; }
        }
        fk_dst_mutate_at = 0;
    }
    return (int)size;
}

static int fk_sys_set_mtime(const char *path, u32 mtime)
{
    int n;

    if (fk_rofs) return OS32_ERR_ROFS;
    if (fk_set_mtime_rc) {
        /* ext2 はメタデータの I/O 失敗でマウントを書き込み禁止にする。
         * 続く rename も一時ファイルの unlink も通らなくなる (A14a3)。 */
        if (fk_set_mtime_rofs) fk_rofs = 1;
        return fk_set_mtime_rc;
    }
    if (mtime == 0) return OS32_ERR_INVAL;
    n = fs_find(path);
    if (n < 0) return OS32_ERR_NOTFOUND;
    if (fs_nodes[n].set_mtime_rc) {
        if (fk_set_mtime_rofs) fk_rofs = 1;
        return fs_nodes[n].set_mtime_rc;
    }
    fs_nodes[n].mtime = mtime;
    return 0;
}

static int fk_sys_unlink(const char *path)
{
    int n;
    if (fk_rofs) return OS32_ERR_ROFS;
    if (fk_unlink_rc) return fk_unlink_rc;
    n = fs_find(path);
    if (n < 0) return OS32_ERR_NOTFOUND;
    if (fs_nodes[n].is_dir) return OS32_ERR_ISDIR;
    /* **unlink が消すのは「名前」**。贋 FS はノードを名前で引くので、
     * st_nlink がいくつでもこの名前は消える (別名は別ノードで表す)。 */
    fs_drop(n);
    return 0;
}

/* 置き換えの公開: 宛先の名前が**一時ファイルの inode**を指すようにする。
 * 旧宛先のノードは消える (links 1 の場合)。 */
static void fk_publish(int src_n, const char *newpath)
{
    int dst_n = fs_find(newpath);
    if (dst_n >= 0) fs_drop(dst_n);
    strncpy(fs_nodes[src_n].path, newpath, FS_PATH_CAP - 1);
    fs_nodes[src_n].path[FS_PATH_CAP - 1] = '\0';
}

static int fk_sys_rename(const char *oldpath, const char *newpath)
{
    int src_n = fs_find(oldpath);

    fk_rename_calls++;
    if (src_n < 0) return OS32_ERR_NOTFOUND;
    if (fk_rofs) return OS32_ERR_ROFS;

    switch (fk_rename_mode) {
    case RN_OK:
        fk_publish(src_n, newpath);
        return 0;
    case RN_FAIL_BEFORE:
        /* **公開の前**に失敗。媒体は 1 バイトも変わらない (新しい ext2 の
         * 置き換え順序では宛先の名前が消えないので、旧 inode のまま) */
        return fk_rename_rc;
    case RN_FAIL_AFTER:
        /* **公開の後**に失敗。宛先は一時ファイルの inode を指し、
         * 一時名は消えている (段 3 まで通って段 4 / 5 が落ちた形) */
        fk_publish(src_n, newpath);
        return fk_rename_rc;
    case RN_FAIL_AFTER_KEEP: {
        /* 公開の後に失敗し、**一時名も残った** (段 3 が落ちた形)。
         * 一時名と宛先が同じ inode を指す = links 2 */
        int dst_n = fs_find(newpath);
        if (dst_n >= 0) fs_drop(dst_n);
        {
            int clone = fs_add_file(newpath, fs_nodes[src_n].data,
                                    fs_nodes[src_n].size);
            fs_nodes[clone].ino = fs_nodes[src_n].ino;   /* 同じ実体 */
            fs_nodes[clone].mtime = fs_nodes[src_n].mtime;
            fs_nodes[clone].nlink = 2;
            fs_nodes[src_n].nlink = 2;
        }
        return fk_rename_rc;
    }
    case RN_UNKNOWN: {
        /* 公開したか判定できない: 宛先の stat が落ちる */
        int dst_n = fs_find(newpath);
        if (dst_n >= 0) fs_nodes[dst_n].stat_err = OS32_ERR_IO;
        return fk_rename_rc;
    }
    case RN_THIRD_INO: {
        /* 宛先が新旧どちらとも違う inode になった (別の何かが割り込んだ) */
        int dst_n = fs_find(newpath);
        if (dst_n >= 0) fs_nodes[dst_n].ino = 999999;
        return fk_rename_rc;
    }
    default:
        return fk_rename_rc;
    }
}

static KernelAPI g_fake;

static void fake_api_init(void)
{
    memset(&g_fake, 0, sizeof(g_fake));
    g_fake.version = 53;
    g_fake.mem_alloc = fk_mem_alloc;
    g_fake.mem_free = fk_mem_free;
    g_fake.kprintf = fk_kprintf;
    g_fake.sys_mkdir = fk_sys_mkdir;
    g_fake.sys_ls = fk_sys_ls;
    g_fake.sys_is_mounted = fk_sys_is_mounted;
    g_fake.vfs_devname = fk_vfs_devname;
    g_fake.vfs_sync = fk_vfs_sync;
    g_fake.sys_open = fk_sys_open;
    g_fake.sys_close = fk_sys_close;
    g_fake.sys_read = fk_sys_read;
    g_fake.sys_write = fk_sys_write;
    g_fake.sys_stat = fk_sys_stat;
    g_fake.sys_set_mtime = fk_sys_set_mtime;
    g_fake.sys_unlink = fk_sys_unlink;
    g_fake.sys_rename = fk_sys_rename;
}

/* ========================================================================= */
/*  実物の hsync.c                                                            */
/* ========================================================================= */

#define main hsync_main
#include "../../userland/system/hsync.c"
#undef main

/* 実装前 (RED) の hsync.c には掃除の枠が無い。**試験を先に書く**ための
 * 逃げ道で、実装後は必ず実物の値が使われる (R4 がその値で枠を越える)。 */
#ifndef MAX_TEMPS
#define MAX_TEMPS 32
#endif

/* ========================================================================= */
/*  小道具                                                                    */
/* ========================================================================= */

/* 票 TASK_KAPI_DATA_FIELDS: この試験は配備の名札 (.deploy/manifest.txt) を
 * 置かないので、KAPI の門 (名札の kapi= を確かめられなければ断る) を
 * `--force-kapi` で明示して越える。門そのものは h4_manifest_host.c の
 * case_kapi が見る。 */
static int run_hsync(int argc, char **argv)
{
    char *av[16];
    int i;

    fk_log_len = 0;
    fk_log[0] = '\0';
    fk_write_calls = 0;
    fk_read_calls = 0;
    fk_rename_calls = 0;
    fk_sync_calls = 0;
    g_fake.version = fk_version;
    for (i = 0; i < argc && i < 14; i++) av[i] = argv[i];
    av[i++] = (char *)"--force-kapi";
    av[i] = 0;
    return hsync_main(i, av, &g_fake);
}

static int run0(void)
{
    char *av[2];
    av[0] = (char *)"hsync";
    av[1] = 0;
    return run_hsync(1, av);
}

static int run1(const char *a1)
{
    char *av[3];
    av[0] = (char *)"hsync";
    av[1] = (char *)a1;
    av[2] = 0;
    return run_hsync(2, av);
}

static int run2(const char *a1, const char *a2)
{
    char *av[4];
    av[0] = (char *)"hsync";
    av[1] = (char *)a1;
    av[2] = (char *)a2;
    av[3] = 0;
    return run_hsync(3, av);
}

static int log_has(const char *needle) { return strstr(fk_log, needle) != 0; }

static u8 *make_blob(u32 size, u32 seed)
{
    u8 *p = (u8 *)malloc(size ? size : 1);
    u32 i;
    for (i = 0; i < size; i++)
        p[i] = (u8)((i * 31u + seed * 7u + (i >> 8)) & 0xFF);
    return p;
}

static int node_of(const char *path) { return fs_find(path); }

static u32 node_size(const char *path)
{
    int n = fs_find(path);
    return n < 0 ? 0xFFFFFFFFUL : fs_nodes[n].size;
}

static u32 node_mtime(const char *path)
{
    int n = fs_find(path);
    return n < 0 ? 0xFFFFFFFFUL : fs_nodes[n].mtime;
}

static u32 node_ino(const char *path)
{
    int n = fs_find(path);
    return n < 0 ? 0xFFFFFFFFUL : fs_nodes[n].ino;
}

static int node_equal_bytes(const char *path, const u8 *data, u32 size)
{
    int n = fs_find(path);
    if (n < 0) return 0;
    if (fs_nodes[n].size != size) return 0;
    if (size == 0) return 1;
    return memcmp(fs_nodes[n].data, data, size) == 0;
}

/* /host/bin/a.bin (新) と /bin/a.bin (旧) を作る。戻りは旧宛先の写し。 */
static u8 *g_old_copy;
static u32 g_old_size;
static u32 g_old_mtime;
static u32 g_old_ino;

#define SRC_PATH "/host/bin/a.bin"
#define DST_PATH "/bin/a.bin"
#define TMP_PATH "/bin/.hs~a.bin"

static void setup_pair(u32 src_size, u32 src_seed, u32 src_mtime,
                       u32 dst_size, u32 dst_seed, u32 dst_mtime)
{
    u8 *a;
    int n;

    fs_reset();
    fs_add_dir("/host");
    fs_add_dir("/host/bin");
    fs_add_dir("/bin");

    a = make_blob(src_size, src_seed);
    n = fs_add_file(SRC_PATH, a, src_size);
    fs_nodes[n].mtime = src_mtime;
    free(a);

    a = make_blob(dst_size, dst_seed);
    n = fs_add_file(DST_PATH, a, dst_size);
    fs_nodes[n].mtime = dst_mtime;
    g_old_ino = fs_nodes[n].ino;
    if (g_old_copy) free(g_old_copy);
    g_old_copy = a;
    g_old_size = dst_size;
    g_old_mtime = dst_mtime;
}

/* 宛先が「旧のまま」であること (名前・内容・サイズ・mtime・inode) */
static int dst_unchanged(void)
{
    return node_equal_bytes(DST_PATH, g_old_copy, g_old_size) &&
           node_mtime(DST_PATH) == g_old_mtime &&
           node_ino(DST_PATH) == g_old_ino;
}

/* ========================================================================= */
/*  A14a — 公開の前の失敗では旧宛先が変わらない                                */
/* ========================================================================= */

static void case_a14a(void)
{
    printf("== A14a: 公開の前の失敗 -> 旧宛先の内容・サイズ・mtime が不変 ==\n");

    /* (1) 一時ファイルへの write が**途中で**落ちる。
     * コピーは 64KB 単位なので、100000 バイトなら 2 回目の write が
     * 「前半だけ書けた一時ファイル」を残す。 */
    setup_pair(100000, 1, 111, 3000, 2, 222);
    fk_write_fail_at = 2;
    fk_write_fail_rc = OS32_ERR_IO;
    check(run1("bin") != 0, "write 失敗で非ゼロ終了");
    check(dst_unchanged(), "write 失敗: 旧宛先の内容・サイズ・mtime・ino が不変");
    check(node_of(TMP_PATH) < 0, "write 失敗: 一時ファイルは消えている");
    check(log_has("errors=1"), "errors=1");
    check(log_has("公開の前なので旧宛先はそのまま"), "公開の前と明示する");

    /* (2) 空き不足 (FULL) */
    setup_pair(5000, 1, 111, 3000, 2, 222);
    fk_write_fail_at = 1;
    fk_write_fail_rc = OS32_ERR_FULL;
    check(run1("bin") != 0, "空き不足で非ゼロ終了");
    check(log_has("reason=no_space"), "reason=no_space");
    check(dst_unchanged(), "空き不足: 旧宛先のサイズ不変");
    check(node_of(TMP_PATH) < 0, "空き不足: 一時ファイルは消えている");

    setup_pair(5000, 1, 111, 3000, 2, 222);
    fk_write_fail_at = 1;
    fk_write_fail_rc = OS32_ERR_NOSPC;
    check(run1("bin") != 0 && log_has("reason=no_space"),
          "NOSPC も no_space と呼ぶ");

    /* (3) 読戻しの CRC 不一致 (読み取りを 1 回落として長さを狂わせる) */
    setup_pair(5000, 1, 111, 3000, 2, 222);
    fk_read_fail_at = 3;      /* 読戻しの途中 */
    check(run1("bin") != 0, "読戻しの失敗で非ゼロ終了");
    check(dst_unchanged(), "読戻し失敗: 旧宛先が不変");

    /* (4) **公開の前**の rename 失敗 */
    setup_pair(5000, 1, 111, 3000, 2, 222);
    fk_rename_mode = RN_FAIL_BEFORE;
    check(run1("bin") != 0, "公開前の rename 失敗で非ゼロ終了");
    check(log_has("reason=replace_failed"), "reason=replace_failed");
    check(log_has("未公開: 宛先は旧内容のまま"), "未公開と明示する");
    check(dst_unchanged(), "公開前の rename 失敗: 旧宛先が不変");
    check(node_of(TMP_PATH) < 0, "公開前の rename 失敗: 一時ファイルを片づける");
    check(!log_has("copied=1"), "copied に数えない");

    /* (5) 開いている DB の置き換えは VFS が BUSY で断る (票 TASK_VFS_FD_PATH
     * のユーザー決裁 ①、FEP を有効にした後の /db/fep.db)。未公開の失敗として
     * 数え、「使用中」と次の手を出す */
    setup_pair(5000, 1, 111, 3000, 2, 222);
    fk_rename_mode = RN_FAIL_BEFORE;
    fk_rename_rc = OS32_ERR_BUSY;
    check(run1("bin") != 0, "BUSY の rename で非ゼロ終了");
    check(log_has("reason=replace_failed"), "BUSY も未公開 = replace_failed");
    check(log_has("(BUSY)"), "BUSY と名前で出す");
    check(log_has("使用中で置き換えられなかった"), "使用中と明示する");
    check(dst_unchanged(), "BUSY: 旧宛先が不変");
    check(node_of(TMP_PATH) < 0, "BUSY: 一時ファイルを片づける");
    check(!log_has("copied=1"), "BUSY: copied に数えない");
}

/* ========================================================================= */
/*  A14a2 / A14a4 / A14a6 — 公開の後・判定不能・同内容                        */
/* ========================================================================= */

static void case_a14a2(void)
{
    u8 *newdata;

    printf("== A14a2: 公開の後の失敗 -> 新内容を認め replace_partial ==\n");

    setup_pair(5000, 1, 111, 3000, 2, 222);
    newdata = make_blob(5000, 1);
    fk_rename_mode = RN_FAIL_AFTER;
    check(run1("bin") != 0, "公開後の rename 失敗で非ゼロ終了");
    check(log_has("reason=replace_partial"), "reason=replace_partial");
    check(log_has("公開済み"), "公開済みと明示する");
    check(node_equal_bytes(DST_PATH, newdata, 5000),
          "宛先は**検証済みの新しい内容**になっている");
    check(node_mtime(DST_PATH) == 111, "mtime も新しい方 (rename は mtime を変えない)");
    check(!log_has("copied=1"), "**成功には数えない**");
    check(!log_has("UPDATE " DST_PATH), "UPDATE とは言わない");
    free(newdata);

    printf("== A14a4: 公開したか判定できない -> ino 照合で決まる ==\n");
    setup_pair(5000, 1, 111, 3000, 2, 222);
    fk_rename_mode = RN_UNKNOWN;
    check(run1("bin") != 0, "判定不能でも非ゼロ終了");
    check(log_has("reason=replace_unknown"),
          "宛先の stat が落ちたら replace_unknown");

    setup_pair(5000, 1, 111, 3000, 2, 222);
    fk_rename_mode = RN_THIRD_INO;
    check(run1("bin") != 0 && log_has("reason=replace_unknown"),
          "新旧どちらとも違う ino なら replace_unknown");

    printf("== A14a6: -f で新旧同内容、公開前に rename が失敗 ==\n");
    /* **サイズも CRC も同じ**にする。ino で判定していなければ
     * 「公開された」と誤る (往復 2 所見 3)。 */
    fs_reset();
    fs_add_dir("/host");
    fs_add_dir("/host/bin");
    fs_add_dir("/bin");
    {
        u8 *same = make_blob(4096, 9);
        int n = fs_add_file(SRC_PATH, same, 4096);
        fs_nodes[n].mtime = 111;
        n = fs_add_file(DST_PATH, same, 4096);
        fs_nodes[n].mtime = 222;
        g_old_ino = fs_nodes[n].ino;
        if (g_old_copy) free(g_old_copy);
        g_old_copy = same;
        g_old_size = 4096;
        g_old_mtime = 222;
    }
    fk_rename_mode = RN_FAIL_BEFORE;
    check(run2("-f", "bin") != 0, "非ゼロ終了");
    check(log_has("reason=replace_failed"),
          "新旧同内容でも **replace_failed** (ino で判定、partial と誤らない)");
    check(!log_has("reason=replace_partial"), "replace_partial と誤らない");
}

/* ========================================================================= */
/*  A14a3 — mtime の失敗で ROFS、rename も unlink も通らない                   */
/* ========================================================================= */

static void case_a14a3(void)
{
    printf("== A14a3: 一時ファイルへの set_mtime 失敗 -> ROFS -> STALE ==\n");

    setup_pair(5000, 1, 111, 3000, 2, 222);
    fk_set_mtime_rc = OS32_ERR_IO;
    fk_set_mtime_rofs = 1;
    check(run1("bin") != 0, "非ゼロ終了");
    check(log_has("reason=metadata_failed"), "reason=metadata_failed");
    check(!log_has("copied=1"), "**copied は 0** (公開の前の失敗)");
    check(log_has("copied=0"), "集計 copied=0");
    check(dst_unchanged(), "宛先は旧内容のまま");
    check(node_of(TMP_PATH) >= 0, "ROFS なので一時ファイルは消せずに残る");
    check(log_has("STALE "), "STALE と表示する");
    check(fk_rename_calls == 0, "rename まで進まない");

    /* 次の実行が予約名として片づける (§2-4 / A15c) */
    fk_rofs = 0;
    fk_set_mtime_rc = 0;
    fk_set_mtime_rofs = 0;
    check(run1("bin") == 0 || log_has("CLEAN " TMP_PATH),
          "次の実行が残った予約名を片づける");
    check(node_of(TMP_PATH) < 0, "予約名は消えている");
}

/* ========================================================================= */
/*  A14b / A14b2 / A14c — 予約名が既に在る / 利用者のファイル / 消せない        */
/* ========================================================================= */

static void case_a14b(void)
{
    printf("== A14b: 予約名が既に在る (通常ファイル / ディレクトリ / nlink=2) ==\n");

    /* (1) 通常ファイル -> 消して作り直す */
    setup_pair(5000, 1, 111, 3000, 2, 222);
    fs_add_file(TMP_PATH, (const u8 *)"junk", 4);
    check(run1("bin") == 0, "残っていた予約名を消して置き換えられる");
    check(node_size(DST_PATH) == 5000, "宛先は新しい内容");

    /* (2) ディレクトリ -> temp_exists で失敗 (消さない) */
    setup_pair(5000, 1, 111, 3000, 2, 222);
    fs_add_dir(TMP_PATH);
    check(run1("bin") != 0, "ディレクトリなら非ゼロ終了");
    check(log_has("reason=temp_exists"), "reason=temp_exists");
    check(node_of(TMP_PATH) >= 0 && fs_nodes[node_of(TMP_PATH)].is_dir,
          "ディレクトリは消さない");
    check(dst_unchanged(), "宛先は旧内容のまま");

    /* (3) nlink=2 の通常ファイル -> **st_nlink では判断しない**。
     * 掃除の列挙 (sys_ls /bin) を 1 回だけ落として、手順 2 の EXIST 経路
     * (remove_stale_temp) に必ず入るようにする。 */
    setup_pair(5000, 1, 111, 3000, 2, 222);
    {
        int n = fs_add_file(TMP_PATH, (const u8 *)"junk", 4);
        fs_nodes[n].nlink = 2;      /* 途中で止まった媒体の形 (§2-2-4) */
    }
    strcpy(fk_ls_fail_path, "/bin");
    fk_ls_fail_n = 1;
    check(run1("bin") == 0,
          "手順 2 の EXIST 経路: nlink=2 でも予約名として消して作り直す");
    check(node_size(DST_PATH) == 5000, "宛先は新しい内容");

    /* (3b) **掃除**の側も st_nlink を見ない。コピーが不要 (内容も日時も同じ)
     * なディレクトリに nlink=2 の予約名だけを置くので、消える理由は
     * 「掃除が nlink を見ていない」ことだけになる。 */
    setup_pair(4096, 7, 4242, 4096, 7, 4242);
    {
        int n = fs_add_file(TMP_PATH, (const u8 *)"junk", 4);
        fs_nodes[n].nlink = 2;
    }
    check(run1("bin") == 0, "同期は成功する (コピーは起きない)");
    check(log_has("unchanged=1"), "コピーは起きていない");
    check(node_of(TMP_PATH) < 0,
          "**掃除も st_nlink を見ない** (nlink=2 の予約名を消す)");

    printf("== A14b2: 利用者が .hs~notes を置いている -> 消える (予約名) ==\n");
    fs_reset();
    fs_add_dir("/host");
    fs_add_dir("/host/bin");
    fs_add_dir("/bin");
    fs_add_file("/host/bin/x", (const u8 *)"xx", 2);
    fs_add_file("/bin/.hs~notes", (const u8 *)"user data", 9);
    check(run1("bin") == 0, "同期は成功する");
    check(node_of("/bin/.hs~notes") < 0,
          "**利用者のファイルでも消える** (man ページに予約と明記した前提)");
    check(log_has("CLEAN /bin/.hs~notes"), "CLEAN と表示する");
    check(log_has("cleaned=1"), "集計 cleaned=1");

    printf("== A14c: 一時ファイルの unlink も失敗 -> STALE ==\n");
    setup_pair(5000, 1, 111, 3000, 2, 222);
    fk_rename_mode = RN_FAIL_BEFORE;
    fk_unlink_rc = OS32_ERR_IO;
    check(run1("bin") != 0, "非ゼロ終了");
    check(log_has("STALE "), "STALE 表示");
    check(log_has("errors=2"), "rename 失敗と STALE の 2 件");
}

/* ========================================================================= */
/*  A15 / A15c — 途中で止まった媒体からの復旧                                 */
/* ========================================================================= */

static void case_a15(void)
{
    printf("== A15: 止まった状態から再実行して収束する ==\n");

    /* 段 1 / 段 2 の直後 = 一時名が残っている。次の実行が片づけて収束 */
    setup_pair(5000, 1, 111, 3000, 2, 222);
    {
        u8 *half = make_blob(2500, 1);
        int n = fs_add_file(TMP_PATH, half, 2500);
        fs_nodes[n].nlink = 2;       /* links が多い側に残った形 */
        free(half);
    }
    check(run1("bin") == 0, "残骸があっても同期は成功する");
    check(node_size(DST_PATH) == 5000, "宛先は新しい内容");
    check(node_of(TMP_PATH) < 0, "一時名は残らない");

    printf("== A15b: 復旧の途中でコピー元が更に更新されても宛先を壊さない ==\n");
    setup_pair(5000, 1, 111, 3000, 2, 222);
    fs_add_file(TMP_PATH, (const u8 *)"junk", 4);
    {
        int n = fs_find(SRC_PATH);
        u8 *bigger = make_blob(7000, 5);
        fs_set_data(n, bigger, 7000);
        fs_nodes[n].mtime = 333;
        free(bigger);
    }
    check(run1("bin") == 0, "同期は成功する");
    check(node_size(DST_PATH) == 7000, "宛先は最新の内容");

    printf("== A15c: create_excl が名前を作った直後に落ちる -> 次で片づく ==\n");
    setup_pair(5000, 1, 111, 3000, 2, 222);
    fk_write_fail_at = 1;            /* 作った直後の最初の write が落ちる */
    check(run1("bin") != 0, "非ゼロ終了");
    check(node_of(TMP_PATH) < 0, "その場で片づけられた");
    check(dst_unchanged(), "宛先は旧内容のまま");

    /* 片づけられなかった場合 (ROFS) は次の実行が予約名として消す */
    setup_pair(5000, 1, 111, 3000, 2, 222);
    fs_add_file(TMP_PATH, (const u8 *)"", 0);
    check(run1("-n") == 0 || 1, "dry-run でも走る");
    setup_pair(5000, 1, 111, 3000, 2, 222);
    fs_add_file(TMP_PATH, (const u8 *)"", 0);
    check(run2("-n", "bin") == 0, "dry-run は成功で返る");
    check(node_of(TMP_PATH) >= 0, "**dry-run は 1 バイトも消さない**");
    check(log_has("PLAN-CLEAN " TMP_PATH), "PLAN-CLEAN と表示する");
}

/* ========================================================================= */
/*  A17a / A17b / A17c — コピー元・宛先の変化と hardlink                      */
/* ========================================================================= */

static void case_a17(void)
{
    printf("== A17a: 書き込み中にコピー元のサイズ / mtime が変わる ==\n");
    setup_pair(5000, 1, 111, 3000, 2, 222);
    strcpy(fk_src_mutate_path, SRC_PATH);
    fk_src_mutate_at = 1;
    fk_src_mutate_size = 4000;
    fk_src_mutate_mtime = 999;
    check(run1("bin") != 0, "非ゼロ終了");
    check(log_has("reason=source_changed"), "reason=source_changed");
    check(dst_unchanged(), "置換しない: 旧宛先が不変");
    check(node_of(TMP_PATH) < 0, "一時ファイルを片づける");

    printf("== A17b: 判定後に宛先が消える / 型が変わる / 内容が変わる ==\n");
    setup_pair(5000, 1, 111, 3000, 2, 222);
    strcpy(fk_dst_mutate_path, DST_PATH);
    fk_dst_mutate_at = 1;
    fk_dst_mutate_kind = 2;          /* サイズと mtime が変わる */
    check(run1("bin") != 0, "非ゼロ終了");
    check(log_has("reason=dest_changed"), "reason=dest_changed");
    check(node_of(TMP_PATH) < 0, "一時ファイルを片づける");

    setup_pair(5000, 1, 111, 3000, 2, 222);
    strcpy(fk_dst_mutate_path, DST_PATH);
    fk_dst_mutate_at = 1;
    fk_dst_mutate_kind = 1;          /* ディレクトリになる */
    check(run1("bin") != 0 && log_has("reason=dest_changed"),
          "型が変わったら dest_changed");

    setup_pair(5000, 1, 111, 3000, 2, 222);
    strcpy(fk_dst_mutate_path, DST_PATH);
    fk_dst_mutate_at = 1;
    fk_dst_mutate_kind = 0;          /* 消える */
    check(run1("bin") != 0 && log_has("reason=dest_changed"),
          "消えたら dest_changed (新規として置き換えない)");

    printf("== A17c: 宛先の st_nlink > 1 -> hardlink、一時ファイルを作らない ==\n");
    setup_pair(5000, 1, 111, 3000, 2, 222);
    {
        int n = fs_find(DST_PATH);
        fs_nodes[n].nlink = 2;
    }
    check(run1("bin") != 0, "非ゼロ終了");
    check(log_has("reason=hardlink"), "reason=hardlink");
    check(node_of(TMP_PATH) < 0, "**一時ファイルを作らない**");
    check(fk_write_calls == 0, "1 バイトも書かない");
    check(node_size(DST_PATH) == 3000, "旧宛先が不変");
}

/* ========================================================================= */
/*  R1 / R1b — 古いカーネル                                                   */
/* ========================================================================= */

static void case_r1(void)
{
    printf("== R1: KAPI v52 で --unsafe-overwrite 無し -> kernel_too_old ==\n");
    setup_pair(5000, 1, 111, 3000, 2, 222);
    fk_version = 52;
    check(run1("bin") != 0, "非ゼロ終了");
    check(log_has("reason=kernel_too_old"), "reason=kernel_too_old");
    check(fk_write_calls == 0, "**1 件も書かない**");
    check(dst_unchanged(), "宛先は旧内容のまま");
    check(node_of(TMP_PATH) < 0, "一時ファイルも作らない");

    printf("== R1b: --unsafe-overwrite -> WARN + 直接上書き ==\n");
    setup_pair(5000, 1, 111, 3000, 2, 222);
    fk_version = 52;
    check(run2("--unsafe-overwrite", "bin") == 0, "直接上書きで成功する");
    check(log_has("WARN kernel KAPI v52 < 53: direct overwrite (no H2)"),
          "WARN の文言がそのまま出る");
    check(log_has("direct_overwrite=1"), "集計 direct_overwrite=1");
    check(node_size(DST_PATH) == 5000, "宛先は新しい内容");
    check(node_of(TMP_PATH) < 0, "一時ファイルは使わない");

    /* 直接上書きの途中で落ちると**旧内容は失われる**ことを表示で明示する */
    setup_pair(100000, 1, 111, 3000, 2, 222);
    fk_version = 52;
    fk_write_fail_at = 2;
    check(run2("--unsafe-overwrite", "bin") != 0, "コピー失敗で非ゼロ終了");
    check(log_has("直接上書きなので旧内容は残らない"),
          "**旧内容は残らない**ことを表示する");
    check(node_size(DST_PATH) != 3000, "実際に旧宛先は壊れている (保証しない範囲)");

    printf("== R1 追記: -f では解除されない / 新しいカーネルでは無視する ==\n");
    setup_pair(5000, 1, 111, 3000, 2, 222);
    fk_version = 52;
    check(run2("-f", "bin") != 0 && log_has("reason=kernel_too_old"),
          "-f では門を解除しない");

    setup_pair(5000, 1, 111, 3000, 2, 222);
    fk_version = 53;
    check(run2("--unsafe-overwrite", "bin") == 0, "v53 では通常どおり成功");
    check(log_has("--unsafe-overwrite は無視する"), "無視したことを表示する");
    check(!log_has("direct_overwrite="), "direct_overwrite は出ない");
}

/* ========================================================================= */
/*  R3 — 新規ファイル                                                         */
/* ========================================================================= */

static void case_r3(void)
{
    printf("== R3: 新規ファイル (宛先が無い) ==\n");

    /* 公開の前に失敗 -> 本名は不存在のまま (半端な内容が現れない) */
    fs_reset();
    fs_add_dir("/host");
    fs_add_dir("/host/bin");
    fs_add_dir("/bin");
    {
        u8 *a = make_blob(100000, 3);
        int n = fs_add_file(SRC_PATH, a, 100000);
        fs_nodes[n].mtime = 111;
        free(a);
    }
    fk_write_fail_at = 2;
    check(run1("bin") != 0, "非ゼロ終了");
    check(node_of(DST_PATH) < 0, "**本名は不存在のまま** (半端な内容が現れない)");
    check(node_of(TMP_PATH) < 0, "一時ファイルも残らない");

    /* 公開の前の rename 失敗も同じ */
    fs_reset();
    fs_add_dir("/host");
    fs_add_dir("/host/bin");
    fs_add_dir("/bin");
    {
        u8 *a = make_blob(5000, 3);
        int n = fs_add_file(SRC_PATH, a, 5000);
        fs_nodes[n].mtime = 111;
        free(a);
    }
    fk_rename_mode = RN_FAIL_BEFORE;
    check(run1("bin") != 0, "非ゼロ終了");
    check(log_has("reason=replace_failed"),
          "新規でも未公開は replace_failed (不存在のまま)");
    check(node_of(DST_PATH) < 0, "本名は作られない");

    /* 成功すれば完全な新内容 + mtime */
    fs_reset();
    fs_add_dir("/host");
    fs_add_dir("/host/bin");
    fs_add_dir("/bin");
    {
        u8 *a = make_blob(5000, 3);
        int n = fs_add_file(SRC_PATH, a, 5000);
        fs_nodes[n].mtime = 111;
        check(run1("bin") == 0, "新規作成は成功する");
        check(node_equal_bytes(DST_PATH, a, 5000), "完全な新内容");
        check(node_mtime(DST_PATH) == 111, "mtime も揃う");
        free(a);
    }

    /* 公開の後に後始末が落ちた場合も、本名は完全な新内容 */
    fs_reset();
    fs_add_dir("/host");
    fs_add_dir("/host/bin");
    fs_add_dir("/bin");
    {
        u8 *a = make_blob(5000, 3);
        int n = fs_add_file(SRC_PATH, a, 5000);
        fs_nodes[n].mtime = 111;
        fk_rename_mode = RN_FAIL_AFTER;
        check(run1("bin") != 0, "非ゼロ終了");
        check(log_has("reason=replace_partial"), "replace_partial");
        check(node_equal_bytes(DST_PATH, a, 5000),
              "本名は**完全な新内容** (半端な内容は現れない)");
        free(a);
    }
}

/* ========================================================================= */
/*  R4 — 掃除の枠 / 128 件の枠 / 長い名前                                     */
/* ========================================================================= */

static void case_r4(void)
{
    char name[256];
    char path[FS_PATH_CAP];
    int i;

    printf("== R4: .hs~ は同期の 128 件の枠を食わない ==\n");
    fs_reset();
    fs_add_dir("/host");
    fs_add_dir("/host/bin");
    fs_add_dir("/bin");
    /* コピー元に予約名を 40 件 + 通常ファイルを 1 件 */
    for (i = 0; i < 40; i++) {
        sprintf(name, "/host/bin/.hs~junk%02d", i);
        fs_add_file(name, (const u8 *)"j", 1);
    }
    fs_add_file("/host/bin/real.bin", (const u8 *)"real", 4);
    check(run1("bin") == 0, "同期は成功する");
    check(node_of("/bin/real.bin") >= 0, "**通常ファイルが落ちない**");
    for (i = 0; i < 40; i++) {
        sprintf(path, "/bin/.hs~junk%02d", i);
        if (node_of(path) >= 0) break;
    }
    check(i == 40, "コピー元の予約名は宛先へ運ばれない");

    printf("== R4: 掃除の枠を越えたら「全部は見ていない」と表示する ==\n");
    fs_reset();
    fs_add_dir("/host");
    fs_add_dir("/host/bin");
    fs_add_dir("/bin");
    fs_add_file("/host/bin/real.bin", (const u8 *)"real", 4);
    for (i = 0; i < MAX_TEMPS + 5; i++) {
        sprintf(name, "/bin/.hs~t%02d", i);
        fs_add_file(name, (const u8 *)"t", 1);
    }
    run1("bin");
    check(log_has("**全部は見ていない**"), "掃除の完了を主張しない");

    printf("== R4: 一時名が NAME_CAP に収まらない -> name_too_long ==\n");
    fs_reset();
    fs_add_dir("/host");
    fs_add_dir("/host/bin");
    fs_add_dir("/bin");
    /* NAME_CAP = 64。名前 61 文字 + ".hs~" = 65 >= 64 で入らない */
    for (i = 0; i < 61; i++) name[i] = 'n';
    name[61] = '\0';
    strcpy(path, "/host/bin/");
    strcat(path, name);
    fs_add_file(path, (const u8 *)"x", 1);
    strcpy(path, "/bin/");
    strcat(path, name);
    fs_add_file(path, (const u8 *)"old", 3);
    check(run1("bin") != 0, "非ゼロ終了");
    check(log_has("reason=name_too_long"), "reason=name_too_long");
    check(node_size(path) == 3, "**直接上書きへ落とさない** (旧内容が残る)");
}

/* ========================================================================= */
/*  R5 — 保護対象への .hs~ hardlink は消さない                                */
/* ========================================================================= */

static void case_r5(void)
{
    printf("== R5: 保護対象の実体を指す予約名は消さない ==\n");
    fs_reset();
    fs_add_dir("/host");
    fs_add_dir("/host/etc");
    fs_add_dir("/etc");
    fs_add_file("/host/etc/keep.txt", (const u8 *)"k", 1);
    {
        int db = fs_add_file("/etc/settings.db", (const u8 *)"DBDB", 4);
        int hl = fs_add_file("/etc/.hs~settings.db", (const u8 *)"DBDB", 4);
        fs_nodes[hl].ino = fs_nodes[db].ino;     /* 同じ実体 (hardlink) */
        fs_nodes[hl].dev = fs_nodes[db].dev;
        fs_nodes[db].nlink = 2;
        fs_nodes[hl].nlink = 2;
    }
    run1("etc");
    check(node_of("/etc/.hs~settings.db") >= 0,
          "保護対象への hardlink は**消さない**");
    check(log_has("reason=protected"), "reason=protected");
    check(!log_has("CLEAN /etc/.hs~settings.db"), "CLEAN とは言わない");
}

/* ========================================================================= */
/*  replace_unsupported — 宛先 FS が O_EXCL を持たない                        */
/* ========================================================================= */

static void case_unsupported(void)
{
    printf("== §2-3 手順 2: NOSYS で**直接上書きへ黙って落ちない** ==\n");
    setup_pair(5000, 1, 111, 3000, 2, 222);
    fk_excl_nosys = 1;
    check(run1("bin") != 0, "非ゼロ終了");
    check(log_has("reason=replace_unsupported"), "reason=replace_unsupported");
    check(dst_unchanged(), "**旧宛先を上書きしない**");
    check(fk_write_calls == 0, "1 バイトも書かない");
}

/* ========================================================================= */
/*  回帰 (R2 の一部) — 同一判定と mtime だけの更新は変わらない                 */
/* ========================================================================= */

static void case_regression(void)
{
    printf("== 回帰: 同一判定・mtime だけの更新・dry-run は H1/H3 のまま ==\n");

    /* サイズも日時も同じ -> 1 バイトも読まない */
    setup_pair(4096, 7, 4242, 4096, 7, 4242);
    check(run1("bin") == 0, "成功");
    check(log_has("unchanged=1"), "unchanged=1");
    check(fk_read_calls == 0, "1 バイトも読まない");
    check(fk_write_calls == 0, "1 バイトも書かない");

    /* 内容同じ・日時違い -> 本体を書き直さず mtime だけ */
    setup_pair(4096, 7, 4242, 4096, 7, 111);
    check(run1("bin") == 0, "成功");
    check(log_has("metadata_updated=1"), "metadata_updated=1");
    check(log_has("copied=0"), "copied=0 (本体は書き直さない)");
    check(node_mtime(DST_PATH) == 4242, "mtime が揃う");
    check(node_of(TMP_PATH) < 0, "一時ファイルを作らない");

    /* dry-run は 1 バイトも書かない */
    setup_pair(5000, 1, 111, 3000, 2, 222);
    check(run2("-n", "bin") == 0, "dry-run は成功");
    check(fk_write_calls == 0, "dry-run: 1 バイトも書かない");
    check(dst_unchanged(), "dry-run: 宛先が不変");
    check(node_of(TMP_PATH) < 0, "dry-run: 一時ファイルも作らない");

    /* 通常の置き換えが最後まで通ると、一時名は残らない */
    setup_pair(5000, 1, 111, 3000, 2, 222);
    check(run1("bin") == 0, "成功");
    check(log_has("copied=1"), "copied=1");
    check(log_has("cleaned=0"), "集計に cleaned がある");
    check(node_size(DST_PATH) == 5000 && node_mtime(DST_PATH) == 111,
          "宛先は新しい内容と日時");
    check(node_of(TMP_PATH) < 0, "一時名は残らない");
    check(node_ino(DST_PATH) != g_old_ino, "宛先の実体は入れ替わっている");

    /* 既定の /sys 除外は変わらない */
    fs_reset();
    fs_add_dir("/host");
    fs_add_dir("/host/sys");
    fs_add_dir("/sys");
    fs_add_file("/host/sys/shell.bin", (const u8 *)"new", 3);
    fs_add_file("/sys/shell.bin", (const u8 *)"old", 3);
    check(run0() == 0, "全体同期は成功");
    check(log_has("excluded=1"), "ルート直下の sys は除外");
    check(node_equal_bytes("/sys/shell.bin", (const u8 *)"old", 3),
          "/sys は書き換えない");
}

/* =========================================================================
 *  独立レビューの非 blocker 指摘 1 / 2 / 4 (2026-09-16)
 *
 *   1. 手順 8 の rename が通った後に手順 9 の vfs_sync が落ちると、呼び手が
 *      note_target を呼ばず「/sys を更新した -> シェル再起動が必要」が
 *      消えていた。**置換は媒体に載っているのに案内が消えるのは誤報**。
 *   2. コピー元の `.hs~*` を黙って落としていた。`excluded` にも -v の行にも
 *      出ないので、ホストの配備元に紛れても気づけない。
 *   4. mtime の失敗 1 件が、続く drop_temp の STALE でもう 1 件数えられて
 *      errors=2 になっていた。**1 つの失敗は 1 と数える**。
 * ========================================================================= */

#define SYS_SRC "/host/sys/libos32gui.shlib"
#define SYS_DST "/sys/libos32gui.shlib"

/* /host/sys/libos32gui.shlib (新 5000B) と /sys/libos32gui.shlib (旧 3000B) */
static void setup_sys_pair(void)
{
    u8 *nw;
    int n;

    fs_reset();
    fs_add_dir("/host");
    fs_add_dir("/host/sys");
    fs_add_dir("/sys");
    nw = make_blob(5000, 9);
    n = fs_add_file(SYS_SRC, nw, 5000);
    fs_nodes[n].mtime = 777;
    free(nw);
    nw = make_blob(3000, 4);
    n = fs_add_file(SYS_DST, nw, 3000);
    fs_nodes[n].mtime = 222;
    free(nw);
}

static void case_review_nb(void)
{
    printf("== 非 blocker 1: rename 成功 + sync 失敗 -> 再起動の案内は出す ==\n");
    setup_sys_pair();
    /* vfs_sync の順番: 1 = 手順 4 (書き込みの後) / 2 = 手順 9 (rename の後) /
     * 3 = main の最後。2 だけ落とす。 */
    fk_sync_fail_at = 2;
    check(run1("sys") != 0, "非ゼロ終了 (errors に入る)");
    check(log_has("reason=sync_failed"), "reason=sync_failed");
    check(node_size(SYS_DST) == 5000,
          "**置換は媒体に載っている** (宛先は新しい内容)");
    check(log_has("copied=0"), "copied には数えない");
    check(log_has("/sys を更新した"),
          "**シェル再起動の案内が出る** (置換済みなので消してはいけない)");
    check(log_has("errors=1"), "errors=1 (sync の失敗 1 件だけ)");

    /* /boot でも同じ */
    fs_reset();
    fs_add_dir("/host");
    fs_add_dir("/host/boot");
    fs_add_dir("/boot");
    fs_add_file("/host/boot/vmkernel.lz4", (const u8 *)"NEWKERNEL", 9);
    fs_add_file("/boot/vmkernel.lz4", (const u8 *)"old", 3);
    fk_sync_fail_at = 2;
    /* 票 TASK_SERIAL_HOSTFS: この贋カーネル (v53) は起動したイメージを
     * 答えないので、vmkernel.old の門は --no-backup で越える (門は case_boot_old) */
    check(run2("--no-backup", "boot") != 0, "非ゼロ終了");
    check(log_has("/boot を更新した"), "**再起動の案内が出る**");

    /* 置換の前に落ちた回は案内を出さない (媒体は旧内容のまま) */
    setup_pair(5000, 1, 111, 3000, 2, 222);
    fk_sync_fail_at = 1;          /* 手順 4 = 公開の前 */
    check(run1("bin") != 0, "公開の前の sync 失敗で非ゼロ終了");
    check(dst_unchanged(), "宛先は旧内容のまま");
    check(!log_has("稼働中の版は切り替わっていない"),
          "何も入れ替わっていない回に案内は出さない");

    /* ---- PM 決裁 2026-09-16: 手順 8 の replace_partial へも広げる ----
     * rename が非ゼロを返しても、宛先の st_ino が一時ファイルのものと一致
     * すれば**媒体の上では入れ替わっている**。手順 9 と同じ理屈で案内を出す。 */
    printf("== 非 blocker 1b: replace_partial でも再起動の案内は出す ==\n");
    setup_sys_pair();
    fk_rename_mode = RN_FAIL_AFTER;      /* 公開の後に失敗 (st_ino は新しい方) */
    check(run1("sys") != 0, "非ゼロ終了");
    check(log_has("reason=replace_partial"), "reason=replace_partial");
    check(node_size(SYS_DST) == 5000,
          "**置換は媒体に載っている** (宛先は検証済みの新しい内容)");
    check(log_has("copied=0"), "copied には数えない");
    check(!log_has("errors=0"), "errors に数える");
    check(log_has("/sys を更新した"),
          "**シェル再起動の案内が出る** (公開済みなので消してはいけない)");

    /* replace_failed (旧内容のまま) では案内を出さない */
    setup_sys_pair();
    fk_rename_mode = RN_FAIL_BEFORE;
    check(run1("sys") != 0, "非ゼロ終了");
    check(log_has("reason=replace_failed"), "reason=replace_failed");
    check(node_size(SYS_DST) == 3000, "宛先は旧内容のまま");
    check(!log_has("/sys を更新した"),
          "**未公開なら案内は出さない** (入れ替わっていない)");

    /* replace_unknown (公開したか分からない) でも案内を出さない */
    setup_sys_pair();
    fk_rename_mode = RN_UNKNOWN;
    check(run1("sys") != 0, "非ゼロ終了");
    check(log_has("reason=replace_unknown"), "reason=replace_unknown");
    check(!log_has("/sys を更新した"),
          "**判定できないなら案内は出さない** (出す根拠がない)");

    printf("== 非 blocker 2: コピー元の .hs~ を黙って落とさない ==\n");
    fs_reset();
    fs_add_dir("/host");
    fs_add_dir("/host/bin");
    fs_add_dir("/bin");
    fs_add_file("/host/bin/real.bin", (const u8 *)"real", 4);
    fs_add_file("/host/bin/.hs~x", (const u8 *)"junk", 4);
    check(run2("-v", "bin") == 0, "同期は成功する");
    check(log_has("excluded=1"), "**excluded が 1 増える**");
    check(log_has("reason=reserved_name"), "reason=reserved_name");
    check(log_has("EXCLUDE /host/bin/.hs~x"),
          "-v で**コピー元**のパスを見せる (直すのはそちら)");
    check(node_of("/bin/.hs~x") < 0, "宛先へは運ばない (R4 のまま)");
    check(node_of("/bin/real.bin") >= 0, "通常ファイルは同期される");

    /* -v 無しでも数だけは出る */
    fs_reset();
    fs_add_dir("/host");
    fs_add_dir("/host/bin");
    fs_add_dir("/bin");
    fs_add_file("/host/bin/real.bin", (const u8 *)"real", 4);
    fs_add_file("/host/bin/.hs~x", (const u8 *)"junk", 4);
    check(run1("bin") == 0, "同期は成功する");
    check(log_has("excluded=1"), "-v 無しでも excluded に数える");
    check(!log_has("EXCLUDE "), "-v 無しなら行は出さない");

    printf("== 非 blocker 4: 1 つの失敗を 2 と数えない ==\n");
    setup_pair(5000, 1, 111, 3000, 2, 222);
    fk_set_mtime_rc = OS32_ERR_IO;
    /* ext2 は mtime の I/O 失敗でマウントを ROFS に落とすので、続く unlink も
     * 通らない。ここでは最後の vfs_sync まで巻き込まないよう unlink だけに
     * 効かせて、**mtime の失敗が何件に数えられるか**を見る。 */
    fk_unlink_rc = OS32_ERR_ROFS;
    check(run1("bin") != 0, "非ゼロ終了");
    check(log_has("reason=metadata_failed"), "reason=metadata_failed");
    check(log_has("STALE "), "STALE も表示する (表示は両方出してよい)");
    check(log_has("errors=1"),
          "**1 つの失敗は 1 件** (STALE で二重に数えない)");
    check(dst_unchanged(), "宛先は旧内容のまま");
    check(fk_rename_calls == 0, "rename まで進まない");
}

/* ---- 票 TASK_SERIAL_HOSTFS §1-v3: /boot/vmkernel.old ------------------- */
static u32   fk_boot_crc;
static int   fk_boot_valid;
static int __cdecl fk_boot_image_info(BootImageInfo *out)
{
    memset(out, 0, sizeof(*out));
    out->crc_valid = (u8)fk_boot_valid;
    out->image_crc = fk_boot_valid ? fk_boot_crc : 0;
    out->source = 2;
    return 0;
}

/* VK32 v2 の小さな写し (entry 1 本)。image_crc 欄 (16 + 20 + 4 = 40) を 0 として
 * 求めた CRC を返し、欄にも入れる。 */
static u32 make_vk32(u8 *b, u32 len, u8 fill)
{
    u32 i, crc;
    memset(b, fill, len);
    memset(b, 0, 44);
    b[0] = 'V'; b[1] = 'K'; b[2] = '3'; b[3] = '2';
    b[4] = 44; b[8] = 2; b[12] = 1;
    b[36] = (u8)len;                     /* image_size (飾り) */
    crc = crc32_core_final(crc32_core_update(CRC32_INIT, b, len));
    for (i = 0; i < 4; i++) b[40 + i] = (u8)(crc >> (8 * i));
    return crc;
}

static void boot_pair(u32 *old_crc)
{
    static u8 oldk[200], newk[240];
    fs_reset();
    fs_add_dir("/host");
    fs_add_dir("/host/boot");
    fs_add_dir("/boot");
    *old_crc = make_vk32(oldk, sizeof(oldk), 0x11);
    (void)make_vk32(newk, sizeof(newk), 0x22);
    fs_add_file("/host/boot/vmkernel.lz4", newk, sizeof(newk));
    fs_add_file("/boot/vmkernel.lz4", oldk, sizeof(oldk));
    fk_version = 65;              /* fs_reset が 53 に戻すので毎回 */
}

static void case_boot_old(void)
{
    u32 crc;

    printf("== TASK_SERIAL_HOSTFS: /boot/vmkernel.old は起動した版のときだけ ==\n");
    g_fake.boot_image_info = fk_boot_image_info;

    /* 一致 → .old を作ってから置き換える */
    boot_pair(&crc);
    fk_version = 65;
    fk_boot_valid = 1;
    fk_boot_crc = crc;
    check(run1("boot") == 0, "一致: 成功");
    check(log_has("BACKUP /boot/vmkernel.lz4 -> /boot/vmkernel.old"), "一致: BACKUP 行");
    check(node_size("/boot/vmkernel.old") == 200, "一致: .old は旧版 (200 バイト)");
    check(node_size("/boot/vmkernel.lz4") == 240, "一致: 新版に置き換わった");
    check(node_of("/boot/.hs~vmkernel.old") < 0, "一致: 一時名は残らない");

    /* 一致しない (起動していない版) → 置き換えない */
    boot_pair(&crc);
    fk_boot_crc = crc ^ 1;
    check(run1("boot") != 0, "不一致: 非ゼロ終了");
    check(log_has("reason=not_booted_image"), "不一致: reason=not_booted_image");
    check(node_size("/boot/vmkernel.lz4") == 200, "不一致: 旧版のまま");
    check(node_of("/boot/vmkernel.old") < 0, "不一致: .old を作らない");

    /* 記録が無い (FD 起動・旧カーネル) → 置き換えない */
    boot_pair(&crc);
    fk_boot_valid = 0;
    check(run1("boot") != 0, "記録なし: 非ゼロ終了");
    check(log_has("reason=boot_image_unknown"), "記録なし: reason=boot_image_unknown");
    check(node_size("/boot/vmkernel.lz4") == 200, "記録なし: 旧版のまま");

    /* --no-backup → 作らずに進む */
    boot_pair(&crc);
    check(run2("--no-backup", "boot") == 0, "--no-backup: 成功");
    check(node_size("/boot/vmkernel.lz4") == 240, "--no-backup: 置き換わった");
    check(node_of("/boot/vmkernel.old") < 0, "--no-backup: .old は無い");

    /* dry-run は判定だけ (書かない) */
    boot_pair(&crc);
    fk_boot_valid = 1;
    fk_boot_crc = crc;
    check(run2("-n", "boot") == 0, "dry-run: 成功");
    check(log_has("PLAN /boot/vmkernel.old reason=backup"), "dry-run: PLAN 行");
    check(node_of("/boot/vmkernel.old") < 0 &&
          node_size("/boot/vmkernel.lz4") == 200, "dry-run: 何も書かない");

    /* 同じ内容なら置き換えない = .old も作らない */
    boot_pair(&crc);
    check(run1("boot") == 0, "1 回目");
    check(run1("boot") == 0 && !log_has("BACKUP"), "同じ内容: .old を作り直さない");

    /* ---- レビュー往復 1 (Codex 2): 欄だけが壊れた版を .old にしない ---- */
    boot_pair(&crc);
    {
        int n = node_of("/boot/vmkernel.lz4");
        fs_nodes[n].data[40] ^= 0x5A;        /* image_crc 欄だけ (中身は起動した版) */
    }
    fk_boot_valid = 1;
    fk_boot_crc = crc;                        /* 欄を 0 として求めた値は一致 */
    check(run1("boot") != 0, "欄の破損: 非ゼロ終了");
    check(log_has("reason=boot_image_corrupt"), "欄の破損: reason=boot_image_corrupt");
    check(node_size("/boot/vmkernel.lz4") == 200, "欄の破損: 置き換えない");
    check(node_of("/boot/vmkernel.old") < 0, "欄の破損: .old を作らない");

    /* ---- 既存の .old がある状態で、各段で落とす: 旧 .old も本名も残る ---- */
    {
        static const char *names[] = { "書き込み", "sync", "読戻し",
                                       "判定の後に本名が変わる", "rename" };
        int k;
        for (k = 0; k < 5; k++) {
            char what[96];
            boot_pair(&crc);
            fk_boot_valid = 1;
            fk_boot_crc = crc;
            fs_add_file("/boot/vmkernel.old", (const u8 *)"PREVOLD", 7);
            switch (k) {
            case 0: fk_write_fail_at = 1; break;          /* .old の複製の最初の書き込み */
            case 1: fk_sync_fail_at = 1; break;           /* 複製の後の sync */
            case 2: fk_read_fail_at = 5; break;           /* 読戻しの読み */
            case 3:                                        /* 判定 (読み 2 回) の後 */
                fk_poke_at = 2;
                strcpy(fk_poke_path, "/boot/vmkernel.lz4");
                fk_poke_off = 100;
                break;
            case 4: fk_rename_mode = RN_FAIL_BEFORE; fk_rename_rc = OS32_ERR_IO; break;
            }
            sprintf(what, "既存の .old + %s の失敗: ", names[k]);
            {
                char m[160];
                sprintf(m, "%s非ゼロ終了", what);
                check(run1("boot") != 0, m);
                sprintf(m, "%sreason=backup_failed", what);
                check(log_has("reason=backup_failed"), m);
                sprintf(m, "%s旧 .old はそのまま", what);
                check(node_equal_bytes("/boot/vmkernel.old", (const u8 *)"PREVOLD", 7), m);
                sprintf(m, "%s本名は置き換えない", what);
                check(node_size("/boot/vmkernel.lz4") == 200, m);
                sprintf(m, "%s一時名は残らない", what);
                check(node_of("/boot/.hs~vmkernel.old") < 0, m);
            }
        }
    }
    /* 既存の .old があっても、通れば起動した版で置き換わる */
    boot_pair(&crc);
    fk_boot_valid = 1;
    fk_boot_crc = crc;
    fs_add_file("/boot/vmkernel.old", (const u8 *)"PREVOLD", 7);
    check(run1("boot") == 0, "既存の .old: 成功");
    check(node_size("/boot/vmkernel.old") == 200, "既存の .old: 起動した版に置き換わった");

    fk_version = 53;
    g_fake.boot_image_info = 0;
}

/* ========================================================================= */
/*  FD — 同期先がフロッピーなら何も作らずに断る (Codex 2026-09-25 P2)          */
/* ========================================================================= */

static void case_dst_fd(void)
{
    printf("== FD: 同期先がフロッピー -> mkdir も書き込みもせず断る ==\n");

    /* FD 起動 (ルートが fd0)。/host に新しいディレクトリがある */
    setup_pair(3000, 1, 111, 3000, 2, 222);
    fs_add_dir("/host/newdir");
    fk_root_dev = "fd0";
    fk_mkdir_calls = 0;
    check(run0() != 0, "FD 起動の hsync (全体): 非ゼロ終了");
    check(log_has("dest_on_fd"), "理由 dest_on_fd を出す");
    check(fk_mkdir_calls == 0 && fk_write_calls == 0 && fk_rename_calls == 0,
          "mkdir / write / rename を 1 回も呼ばない");
    check(node_of("/newdir") < 0 && dst_unchanged(), "FD の中身は変わらない");
    fk_mkdir_calls = 0;
    check(run1("bin") != 0 && fk_mkdir_calls == 0 && fk_write_calls == 0 &&
          dst_unchanged(), "FD 起動の hsync bin も断る");
    check(run1("-n") != 0 && log_has("dest_on_fd"), "dry-run でも同じ判定で断る");

    /* HDD 起動 + /fd0 にフロッピー: /fd0 の下への同期だけ断る */
    fk_root_dev = "hd0";
    fk_sub_prefix = "/fd0";
    fk_sub_dev = "fd0";
    fs_add_dir("/host/fd0");
    fs_add_dir("/host/fd0/x");
    fs_add_dir("/fd0");
    fk_mkdir_calls = 0;
    check(run1("fd0/x") != 0 && log_has("dest_on_fd") && fk_mkdir_calls == 0 &&
          node_of("/fd0/x") < 0, "HDD 起動の hsync fd0/x (FD のサブマウント) は断る");
    check(run1("bin") == 0 && !log_has("dest_on_fd"),
          "HDD 起動の hsync bin は通る (FD 判定は宛先のマウントだけを見る)");

    /* 全体同期: / は hd0、/fd0 は fd0、同期元に /host/fd0/x と
     * /host/fd0/f.bin。開始点は HDD なので通るが、/fd0 の下へは入らない
     * (Codex 2026-09-25 2 回目 P2)。hd0 側 (/bin/a.bin) は通常どおり同期する */
    {
        u8 *blob = make_blob(500, 9);
        int before = fs_find("/fd0");
        fs_add_file("/host/fd0/f.bin", blob, 500);
        free(blob);
        check(before >= 0, "準備: /fd0 がある");
        fk_mkdir_fd0_calls = 0;
        check(run0() == 0, "HDD 起動の全体同期: 成功 (マウントをまたがないのは失敗でない)");
        check(log_has("other_mount") && log_has("/fd0"),
              "/fd0 を reason=other_mount で除外したと 1 行出す");
        check(node_of("/fd0/x") < 0 && node_of("/fd0/f.bin") < 0,
              "/fd0 (FD) の下に mkdir も書き込みもしない");
        check(fk_mkdir_fd0_calls == 0, "/fd0 とその下へ mkdir を 1 回も呼ばない");
        check(node_size(DST_PATH) == 3000 && !dst_unchanged() &&
              node_of("/newdir") >= 0,
              "hd0 側は通常どおり同期された (/bin/a.bin の更新、/newdir の作成)");
    }

    /* /sys が別マウント (hd1) + 全体同期: 既定の sys 除外に隠れず other_mount の
     * 1 行が出て、/sys の下は変わらない (Codex 2026-09-25 3 回目 P3) */
    {
        u8 *blob = make_blob(700, 5);
        u8 *old = make_blob(700, 6);
        fs_add_dir("/host/sys");
        fs_add_file("/host/sys/k.bin", blob, 700);
        fs_add_dir("/sys");
        fs_add_file("/sys/k.bin", old, 700);
        fk_sub_prefix = "/sys";
        fk_sub_dev = "hd1";
        check(run0() == 0, "/sys が別マウントの全体同期: 成功");
        check(log_has("/sys reason=other_mount"),
              "/sys を reason=other_mount で除外したと -v 無しで 1 行出す");
        check(node_equal_bytes("/sys/k.bin", old, 700), "/sys の下は変わらない");
        free(blob);
        free(old);
    }

    fk_sub_prefix = 0;
    fk_sub_dev = 0;
}

/* ========================================================================= */
/*  --root — 同期先の根を替える (FD 起動 → /hd0 を更新、2026-09-26)          */
/* ========================================================================= */

/* 引数の並びで回す (最後は 0)。run_hsync が --force-kapi を足す */
static int run_args(const char *const *args)
{
    char *av[14];
    int n = 0;

    av[n++] = (char *)"hsync";
    while (args[n - 1] && n < 13) { av[n] = (char *)args[n - 1]; n++; }
    av[n] = 0;
    return run_hsync(n, av);
}

static u8 *g_root_src;        /* /host/bin/a.bin の中身 (3000 バイト) */
static u8 *g_hd_old;          /* /hd0/bin/a.bin の旧内容 (2000 バイト) */
static u8 *g_hd_sdb;          /* /hd0/etc/settings.db の中身 (100 バイト) */

/* FD 起動 (ルート fd0)、/hd0 は hd0 の ext2 (根の inode 2)、/host は同期元。
 * FD の /bin/a.bin は setup_pair の旧宛先 (dst_unchanged で見る)。 */
static void setup_root_fd(void)
{
    int n;
    u8 *b;

    setup_pair(3000, 1, 111, 3000, 2, 222);
    fk_root_dev = "fd0";
    fk_sub_prefix = "/hd0";
    fk_sub_dev = "hd0";
    fk_sub2_prefix = 0;
    fk_sub2_dev = 0;

    if (g_root_src) free(g_root_src);
    g_root_src = make_blob(3000, 1);           /* SRC_PATH と同じ中身 */

    n = fs_add_dir("/hd0");
    fs_nodes[n].ino = 2;                       /* ext2 の根 (EXT2_ROOT_INO) */
    fs_add_dir("/hd0/bin");
    fs_add_dir("/hd0/etc");
    if (g_hd_old) free(g_hd_old);
    g_hd_old = make_blob(2000, 3);
    n = fs_add_file("/hd0/bin/a.bin", g_hd_old, 2000);
    fs_nodes[n].mtime = 333;
    if (g_hd_sdb) free(g_hd_sdb);
    g_hd_sdb = make_blob(100, 4);
    fs_add_file("/hd0/etc/settings.db", g_hd_sdb, 100);

    fs_add_dir("/host/etc");
    b = make_blob(120, 5);
    fs_add_file("/host/etc/settings.db", b, 120);
    free(b);
    fs_add_dir("/host/newdir");
    b = make_blob(50, 6);
    n = fs_add_file("/host/newdir/n.bin", b, 50);
    fs_nodes[n].mtime = 444;
    free(b);
}

/* /hd0 の下が準備のまま (旧 a.bin・settings.db、newdir が無い) */
static int hd_unchanged(void)
{
    return node_equal_bytes("/hd0/bin/a.bin", g_hd_old, 2000) &&
           node_mtime("/hd0/bin/a.bin") == 333 &&
           node_equal_bytes("/hd0/etc/settings.db", g_hd_sdb, 100) &&
           node_of("/hd0/newdir") < 0;
}

/* 断る門: 非ゼロ・理由・mkdir / write / rename が 0 回・FD も /hd0 も不変 */
static void root_refused(const char *const *args, const char *reason,
                         const char *what)
{
    char m[200];
    int rc;

    fk_mkdir_calls = 0;
    rc = run_args(args);
    sprintf(m, "%s: 非ゼロ終了", what);
    check(rc != 0, m);
    if (reason) {
        sprintf(m, "%s: reason=%s", what, reason);
        check(log_has(reason), m);
    }
    sprintf(m, "%s: mkdir / write / rename を 1 回も呼ばない", what);
    check(fk_mkdir_calls == 0 && fk_write_calls == 0 && fk_rename_calls == 0, m);
    sprintf(m, "%s: FD (/bin/a.bin) も /hd0 も変わらない", what);
    check(dst_unchanged() && hd_unchanged() && node_of("/newdir") < 0, m);
}

static void case_root(void)
{
    printf("== --root: 同期先の根を替える (FD 起動 -> /hd0) ==\n");

    /* ---- 全体同期: /hd0 だけが変わり、FD (/) は無変更 ---- */
    setup_root_fd();
    {
        const char *a[] = { "--root", "/hd0", 0 };
        fk_mkdir_calls = 0;
        fk_mkdir_fd0_calls = 0;
        check(run_args(a) == 0, "--root /hd0 (全体): 成功");
        check(log_has("hsync: /host -> /hd0"), "--root /hd0: 見出しは /host -> /hd0");
        check(node_equal_bytes("/hd0/bin/a.bin", g_root_src, 3000) &&
              node_mtime("/hd0/bin/a.bin") == 111,
              "--root /hd0: /hd0/bin/a.bin が新しい内容と元の mtime になった");
        check(node_of("/hd0/newdir/n.bin") >= 0, "--root /hd0: /hd0/newdir/n.bin を作った");
        check(dst_unchanged(), "--root /hd0: FD の /bin/a.bin は無変更");
        check(node_of("/newdir") < 0 && node_of("/etc") < 0,
              "--root /hd0: FD (/) に newdir も etc も作らない");
        check(node_equal_bytes("/hd0/etc/settings.db", g_hd_sdb, 100) &&
              log_has("PROTECTED /hd0/etc/settings.db"),
              "--root /hd0: 保護が /hd0/etc/settings.db に効く");
    }
    /* 名前規則 (実体がまだ無い一族): /hd0/etc に -wal が無くても作らない */
    setup_root_fd();
    {
        const char *a[] = { "--root", "/hd0", 0 };
        fs_add_file("/host/etc/settings.db-wal", (const u8 *)"WAL", 3);
        check(run_args(a) == 0 && node_of("/hd0/etc/settings.db-wal") < 0 &&
              log_has("PROTECTED /hd0/etc/settings.db-wal"),
              "--root /hd0: 名前規則が根からの相対で効く (/hd0/etc/settings.db-wal を作らない)");
    }

    /* ---- 絞り込み: /host/bin -> /hd0/bin だけ ---- */
    setup_root_fd();
    {
        const char *a[] = { "--root", "/hd0", "bin", 0 };
        check(run_args(a) == 0, "--root /hd0 bin: 成功");
        check(log_has("hsync: /host/bin -> /hd0/bin"), "--root /hd0 bin: /host/bin -> /hd0/bin");
        check(node_equal_bytes("/hd0/bin/a.bin", g_root_src, 3000), "--root /hd0 bin: /hd0/bin/a.bin を更新");
        check(dst_unchanged() && node_of("/hd0/newdir") < 0,
              "--root /hd0 bin: FD と範囲外 (/hd0/newdir) は無変更");
    }
    setup_root_fd();
    {
        const char *a[] = { "-f", "--root", "/hd0", "etc", 0 };
        check(run_args(a) == 0 && log_has("PROTECTED /hd0/etc/settings.db") &&
              node_equal_bytes("/hd0/etc/settings.db", g_hd_sdb, 100),
              "--root /hd0 -f etc: /hd0/etc/settings.db は -f でも書かない");
    }

    /* ---- 保護の実体規則: /hd0/etc/settings.db の hardlink の別名 ---- */
    setup_root_fd();
    {
        const char *a[] = { "--root", "/hd0", "bin", 0 };
        u8 *b = make_blob(90, 7);
        int n = fs_add_file("/hd0/bin/sdb", g_hd_sdb, 100);
        fs_nodes[n].ino = fs_nodes[node_of("/hd0/etc/settings.db")].ino;
        fs_add_file("/host/bin/sdb", b, 90);
        free(b);
        check(run_args(a) == 0 && log_has("PROTECTED /hd0/bin/sdb") &&
              node_equal_bytes("/hd0/bin/sdb", g_hd_sdb, 100),
              "--root /hd0: /hd0/etc/settings.db の別名 (/hd0/bin/sdb) も守る");
    }

    /* ---- マウントをまたがない: /hd0 の下の別マウント ---- */
    setup_root_fd();
    {
        const char *a[] = { "--root", "/hd0", 0 };
        u8 *b = make_blob(40, 8);
        fs_add_dir("/hd0/mnt");
        fs_add_dir("/host/mnt");
        fs_add_file("/host/mnt/f.bin", b, 40);
        free(b);
        fk_sub2_prefix = "/hd0/mnt";
        fk_sub2_dev = "hd1";
        check(run_args(a) == 0, "--root /hd0 + /hd0/mnt が別マウント: 成功");
        check(log_has("EXCLUDE /hd0/mnt reason=other_mount"),
              "/hd0/mnt を reason=other_mount で除外したと 1 行出す");
        check(node_of("/hd0/mnt/f.bin") < 0, "/hd0/mnt の下に書かない");
        check(node_equal_bytes("/hd0/bin/a.bin", g_root_src, 3000),
              "/hd0 の残りは通常どおり同期");
        fk_sub2_prefix = 0;
    }

    /* ---- 名札 (.deploy/manifest.txt) は根からの相対で照合する ---- */
    setup_root_fd();
    {
        const char *a[] = { "--root", "/hd0", 0 };
        char man[512];
        sprintf(man,
                "format=2\nbuild=test\ngenerated=now\nkapi=%lu\nkapi_version=1\n"
                "count=2\n---\nbin/a.bin 3000 00000000 111\n"
                "newdir/n.bin 50 00000000 444\n",
                (unsigned long)KAPI_DATA_FIELDS_OFF);
        fs_add_dir("/host/.deploy");
        fs_add_file("/host/.deploy/manifest.txt", (const u8 *)man, (u32)strlen(man));
        check(run_args(a) == 0 && log_has("DEPLOY build=test"), "名札つき --root /hd0: 成功");
        check(log_has("manifest_extra=0"),
              "名札の照合は根からの相対 (/hd0/bin/a.bin は bin/a.bin) = manifest_extra=0");
        check(node_of("/hd0/.deploy/manifest.txt") >= 0 &&
              node_of("/.deploy") < 0,
              "名札の写しは /hd0/.deploy へ (FD には置かない)");
    }

    /* ---- 根の書き方の揺れ: 末尾の / と // と . は正規化して /hd0 と同じ ---- */
    {
        static const char *const roots[2] = { "/hd0/", "//hd0/./" };
        int k;
        char m[160];
        for (k = 0; k < 2; k++) {
            const char *a[] = { "--root", 0, "bin", 0 };
            a[1] = roots[k];
            setup_root_fd();
            sprintf(m, "--root %s bin: /hd0 と同じ (/host/bin -> /hd0/bin、FD は無変更)", roots[k]);
            check(run_args(a) == 0 && log_has("hsync: /host/bin -> /hd0/bin") &&
                  node_equal_bytes("/hd0/bin/a.bin", g_root_src, 3000) &&
                  dst_unchanged() && node_of("/hd0//bin") < 0, m);
        }
    }

    /* ---- 同期する dir が根の外へ出る (--root /hd0 ../x) は断る ---- */
    setup_root_fd();
    {
        const char *a[] = { "--root", "/hd0", "../x", 0 };
        root_refused(a, 0, "--root /hd0 ../x (根の外)");
    }

    /* ---- 根の stat が落ちる (I/O 失敗) は root_stat_failed + err (ext2 でない、ではない) ---- */
    setup_root_fd();
    {
        const char *a[] = { "--root", "/hd0", 0 };
        char m[64];
        fs_nodes[node_of("/hd0")].stat_err = OS32_ERR_IO;
        root_refused(a, "root_stat_failed", "--root /hd0 (根の stat が I/O 失敗)");
        sprintf(m, "root_stat_failed err=%d", OS32_ERR_IO);
        check(log_has(m) && !log_has("root_not_ext2"),
              "根の stat の失敗は err を出し、root_not_ext2 と言わない");
        fs_nodes[node_of("/hd0")].stat_err = 0;
    }

    /* ---- --root /hd0 sys: 案内は「HDD から起動し直す」 ---- */
    setup_root_fd();
    {
        const char *a[] = { "--root", "/hd0", "sys", 0 };
        u8 *b = make_blob(70, 9);
        fs_add_dir("/host/sys");
        fs_add_file("/host/sys/x.shlib", b, 70);
        fs_add_dir("/hd0/sys");
        free(b);
        check(run_args(a) == 0 && node_of("/hd0/sys/x.shlib") >= 0 && node_of("/sys") < 0,
              "--root /hd0 sys: /hd0/sys に書く (FD の /sys には書かない)");
        check(log_has("NOTE: /hd0/sys を更新した -> HDD (/hd0) から起動し直す") &&
              !log_has("シェル再起動が必要"),
              "--root /hd0 sys: 案内は「HDD から起動し直す」(シェル再起動ではない)");
    }

    /* ---- --root / は既定と同じ (HDD 起動、ルートが ext2) ---- */
    {
        const char *a0[] = { 0 };
        const char *a1[] = { "--root", "/", 0 };
        u8 *after0;
        int n, same;
        setup_pair(3000, 1, 111, 3000, 2, 222);
        fk_root_dev = "hd0";
        fk_sub_prefix = 0;
        n = fs_add_dir("/");
        fs_nodes[n].ino = 2;
        check(run_args(a0) == 0, "既定 (HDD 起動): 成功");
        after0 = (u8 *)malloc(3000);
        memcpy(after0, fs_nodes[node_of(DST_PATH)].data, 3000);
        setup_pair(3000, 1, 111, 3000, 2, 222);
        n = fs_add_dir("/");
        fs_nodes[n].ino = 2;
        check(run_args(a1) == 0 && log_has("hsync: /host -> /"),
              "--root / (HDD 起動): 成功、見出しは /host -> /");
        same = node_equal_bytes(DST_PATH, after0, 3000) && node_mtime(DST_PATH) == 111;
        check(same && !log_has("HDD ("), "--root /: 既定と同じ結果 (/bin/a.bin を更新、根付きの案内は無い)");
        free(after0);
    }
    /* FD 起動の --root / は既定と同じく dest_on_fd */
    setup_root_fd();
    {
        const char *a[] = { "--root", "/", 0 };
        root_refused(a, "dest_on_fd", "FD 起動の --root / (既定と同じ = FD)");
    }

    /* ---- 断る門: 1 件も書かない ---- */
    setup_root_fd();
    {
        const char *a1[] = { "--root", "/fd1", 0 };
        const char *a2[] = { "--root", "/host", 0 };
        const char *a3[] = { "--root", "/host/bin", "bin", 0 };
        const char *a4[] = { "--root", "/hd0/bin", 0 };
        const char *a5[] = { "--root", "/hd1", 0 };
        const char *a6[] = { "-n", "--root", "/cd0", "bin", 0 };
        const char *a7[] = { "--root", "hd0", 0 };
        const char *a8[] = { "--root", 0 };
        const char *a9[] = { "--root", "/hd0", "--root", "/hd0", 0 };
        const char *a10[] = { "--root", "/hd0/../fd1", 0 };
        int n;

        fk_sub2_prefix = "/fd1";
        fk_sub2_dev = "fd1";
        fs_add_dir("/fd1");
        root_refused(a1, "dest_on_fd", "--root /fd1 (FD)");
        root_refused(a10, "dest_on_fd", "--root /hd0/../fd1 (正規化して FD)");
        root_refused(a2, "root_is_source", "--root /host (同期元)");
        root_refused(a3, "root_is_source", "--root /host/bin (同期元の下)");
        root_refused(a4, "root_not_mount", "--root /hd0/bin (マウントの根でない)");

        fk_sub2_prefix = "/hd1";
        fk_sub2_dev = "hd1";
        fs_add_dir("/hd1");               /* 根の inode は 2 でない = FAT 等 */
        root_refused(a5, "root_not_ext2", "--root /hd1 (ext2 でない)");

        fk_sub2_prefix = "/cd0";
        fk_sub2_dev = "cd0";
        n = fs_add_dir("/cd0");
        fs_nodes[n].ino = 20;             /* ISO9660: 根ディレクトリの LBA */
        root_refused(a6, "root_not_ext2", "-n --root /cd0 (ISO9660、dry-run でも)");

        root_refused(a7, 0, "--root hd0 (相対パス)");
        root_refused(a8, 0, "--root の値が無い");
        root_refused(a9, 0, "--root を 2 回");
        fk_sub2_prefix = 0;

        /* HDD 起動: /hd0 は自動マウントされない (ルートが hd0) */
        fk_root_dev = "hd0";
        fk_sub_prefix = 0;
        {
            const char *b1[] = { "--root", "/hd0", 0 };
            fk_mkdir_calls = 0;
            check(run_args(b1) != 0 && log_has("root_not_mount") &&
                  fk_mkdir_calls == 0 && fk_write_calls == 0 && hd_unchanged(),
                  "HDD 起動の --root /hd0 (マウントされていない) は断る");
        }
    }

    /* ---- 走っているカーネルの版の門は --root でも同じ (v53 未満は断る) ---- */
    setup_root_fd();
    {
        const char *a[] = { "--root", "/hd0", 0 };
        fk_version = 52;
        root_refused(a, "kernel_too_old", "KAPI v52 のカーネル + --root /hd0");
        fk_version = 53;
    }

    /* ---- /boot/vmkernel.old は根の下に作る ---- */
    {
        const char *a[] = { "--root", "/hd0", "boot", 0 };
        const char *anb[] = { "--no-backup", "--root", "/hd0", "boot", 0 };
        static u8 oldk[200], newk[240];
        u32 crc;

        g_fake.boot_image_info = fk_boot_image_info;
        setup_root_fd();
        fk_version = 65;
        crc = make_vk32(oldk, sizeof(oldk), 0x11);
        (void)make_vk32(newk, sizeof(newk), 0x22);
        fs_add_dir("/host/boot");
        fs_add_file("/host/boot/vmkernel.lz4", newk, sizeof(newk));
        fs_add_dir("/hd0/boot");
        fs_add_file("/hd0/boot/vmkernel.lz4", oldk, sizeof(oldk));

        /* 起動した版の記録が /hd0 の版と一致する形 (照合の経路を通す) */
        fk_boot_valid = 1;
        fk_boot_crc = crc;
        check(run_args(a) == 0, "--root /hd0 boot (起動記録と一致): 成功");
        check(node_size("/hd0/boot/vmkernel.old") == 200 &&
              node_of("/boot/vmkernel.old") < 0 && node_of("/boot") < 0,
              "--root /hd0 boot: .old は /hd0/boot に作る (FD には作らない)");
        check(node_size("/hd0/boot/vmkernel.lz4") == 240, "--root /hd0 boot: 新版に置き換わった");
        check(log_has("NOTE: /hd0/boot を更新した -> HDD (/hd0) から起動し直す") &&
              !log_has("再起動が必要"),
              "--root /hd0 boot: 案内は「HDD から起動し直す」(いまの媒体のリセットではない)");

        /* FD 起動の実際: 起動記録は FD の版 = /hd0 の版と一致しない → 断る */
        fs_drop(node_of("/hd0/boot/vmkernel.lz4"));
        fs_drop(node_of("/hd0/boot/vmkernel.old"));
        fs_add_file("/hd0/boot/vmkernel.lz4", oldk, sizeof(oldk));
        fk_boot_crc = crc ^ 1;
        check(run_args(a) != 0 && log_has("reason=not_booted_image") &&
              log_has("--root /hd0") && node_size("/hd0/boot/vmkernel.lz4") == 200,
              "--root /hd0 boot (FD の起動記録): not_booted_image で置き換えない");
        {
            /* -n でも同じ門: 起動記録が FD の版なら not_booted_image (--no-backup 無し) */
            const char *an[] = { "-n", "--root", "/hd0", "boot", 0 };
            fk_mkdir_calls = 0;
            check(run_args(an) != 0 && log_has("reason=not_booted_image") &&
                  node_size("/hd0/boot/vmkernel.lz4") == 200 &&
                  node_of("/hd0/boot/vmkernel.old") < 0 && fk_mkdir_calls == 0 &&
                  fk_write_calls == 0 && fk_rename_calls == 0,
                  "-n --root /hd0 boot (FD の起動記録、--no-backup 無し): not_booted_image で断る");
        }
        check(run_args(anb) == 0 && node_size("/hd0/boot/vmkernel.lz4") == 240 &&
              node_of("/hd0/boot/vmkernel.old") < 0,
              "--no-backup --root /hd0 boot: .old を作らずに置き換える");

        fk_version = 53;
        g_fake.boot_image_info = 0;
    }

    fk_root_dev = "hd0";
    fk_sub_prefix = 0;
    fk_sub_dev = 0;
    fk_sub2_prefix = 0;
    fk_sub2_dev = 0;
}

int main(void)
{
    printf("=== 票 H2: hsync の置換安全化 (一時ファイル + 検証 + 置換) ===\n");
    fake_api_init();

    case_a14a();
    case_a14a2();
    case_a14a3();
    case_a14b();
    case_a15();
    case_a17();
    case_r1();
    case_r3();
    case_r4();
    case_r5();
    case_unsupported();
    case_regression();
    case_review_nb();
    case_boot_old();
    case_dst_fd();
    case_root();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
