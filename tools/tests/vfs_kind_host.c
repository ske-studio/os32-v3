/* =========================================================================
 *  VFS_KIND_HOST.C — vfs_path_kind() が「読めなかったディレクトリ」を
 *  ファイルと判定していた退行 (B6) を **実物のソースで** 確かめる
 *
 *  票: H1 (docs/tasks/shell/HSYNC_IMPROVEMENT_PLAN.md) / Codex 実装レビュー
 *      往復 3 の B6。`d574704` で入った退行。
 *  実行: python3 -B tools/tests/test_vfs_kind.py
 *  記録: tools/tests/h1_tdd.md
 *
 *  実物の fs/vfs.c をそのまま #include し、境界 (kstring / kmalloc) だけを
 *  同義の C で置く。FS ドライバは合成した VfsOps で、stat / list_dir /
 *  get_file_size の戻り値を 1 つずつ指定できる。
 *
 *  直す前の反例:
 *    stat が IO で失敗 -> 「stat 未対応」とみなして下のプローブへ
 *    -> list_dir が FULL (1000 件超) -> get_file_size は**ディレクトリでも
 *       成功する** -> VFS_KIND_FILE
 *    結果 `cd` が NOTDIR になり、sys_open のディレクトリ拒否をすり抜ける。
 *
 *  ext2 / FAT / ISO9660 の既存の判定が変わらないことも、同じ合成ドライバで
 *  「stat が答えるドライバ」として押さえる (4 つとも stat を持っている)。
 *
 *  さらに **実物の fs/vfs_fd.c も同じ翻訳単位に取り込み**、`vfs_open()` が
 *  種別の判定結果をどう使うかまで通す (往復 4 の B7)。`vfs_path_kind` の
 *  戻り値までしか見ていなかったことが、B7 の検出漏れの原因だった。
 *
 *  エミュレータ・実配備・make には一切触れない。
 * ========================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vfs.h"

/* ---- fs/vfs.c の境界 (lib/kstring_asm.asm / kmalloc はホストで組めない) ---- */
void *kzalloc(u32 size) { return calloc(1, size); }
void kfree(void *p) { free(p); }
void *kmemset(void *dst, int val, u32 n) { return memset(dst, val, n); }
int kstrcmp(const char *a, const char *b) { return strcmp(a, b); }
u32 kstrlen(const char *s) { return (u32)strlen(s); }
char *kstrncpy(char *dst, const char *src, u32 n)
{
    u32 i;
    if (n == 0) return dst;
    for (i = 0; i + 1 < n && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
    return dst;
}
char *kstrncat(char *dst, const char *src, u32 n)
{
    u32 d = (u32)strlen(dst);
    if (d + 1 >= n) return dst;
    kstrncpy(dst + d, src, n - d);
    return dst;
}

#include "../../fs/vfs.c"

/* ---- fs/vfs_fd.c の境界 (コンソール / リダイレクト / 所有者タグ) ---- */
int fd_is_redirected(int fd) { (void)fd; return 0; }
u16 fd_redirect_ifmt(int fd, int *out_file_fd)
{ (void)fd; if (out_file_fd) *out_file_fd = -1; return OS_S_IFCHR; }
int fd_redirect_read(int fd, void *buf, u32 size)
{ (void)fd; (void)buf; (void)size; return VFS_ERR_INVAL; }
int fd_redirect_write(int fd, const void *buf, u32 size)
{ (void)fd; (void)buf; (void)size; return VFS_ERR_INVAL; }
int kbd_getchar(void) { return '\n'; }
void console_write(const char *buf, u32 size, u8 color)
{ (void)buf; (void)size; (void)color; }
int res_owner_get(void) { return 0; }

/* open の**受け手**まで実物を通す (票 H1 / 往復 4 の B7) */
#include "../../fs/vfs_fd.c"

/* ------------------------------------------------------------------------ */

static int failures;
static int checks;

static void check(int cond, const char *name)
{
    checks++;
    printf("  %s %s\n", cond ? "ok  " : "FAIL", name);
    if (!cond) failures++;
}

/* ---- 合成 FS ドライバ ---------------------------------------------------- */

static int   drv_has_stat;       /* 0 … stat を**持たない**ドライバを模す */
static int   drv_stat_rc;
static int   drv_stat_is_dir;
static int   drv_list_rc;
static int   drv_size_rc;
static int   drv_size_calls;
static int   drv_list_calls;

static int drv_stat(void *ctx, const char *path, OS32_Stat *buf)
{
    (void)ctx; (void)path;
    kmemset(buf, 0, sizeof(OS32_Stat));
    if (drv_stat_rc != VFS_OK) return drv_stat_rc;
    buf->st_mode = (u16)(drv_stat_is_dir ? (OS_S_IFDIR | 0755)
                                         : (OS_S_IFREG | 0644));
    return VFS_OK;
}

static int drv_list_dir(void *ctx, const char *path, vfs_dir_cb cb, void *uc)
{
    (void)ctx; (void)path; (void)cb; (void)uc;
    drv_list_calls++;
    return drv_list_rc;
}

/* HostDrv の hdrv_get_file_size と同じ性質: DIRECTORY_FILE を指定せずに
 * 開くので**ディレクトリでも成功する**。 */
static int drv_get_file_size(void *ctx, const char *path, u32 *size)
{
    (void)ctx; (void)path;
    drv_size_calls++;
    if (drv_size_rc != VFS_OK) return drv_size_rc;
    *size = 4096;
    return VFS_OK;
}

static int   drv_write_rc;
static int   drv_write_calls;

static int drv_write_file(void *ctx, const char *path, const void *data, u32 size)
{
    (void)ctx; (void)path; (void)data; (void)size;
    drv_write_calls++;
    return drv_write_rc;
}

static int drv_ctx_tag = 1;
static void *drv_mount(int dev) { (void)dev; return &drv_ctx_tag; }
static void drv_umount(void *ctx) { (void)ctx; }
static int drv_is_mounted(void *ctx) { (void)ctx; return 1; }

static VfsOps drv_ops_with_stat;
static VfsOps drv_ops_no_stat;

/* fs/vfs_fd.c の FD 表を空にする (実物を #include しているので直に触れる)。
 * generation は据え置き — 使い切りの検査はここの対象ではない。 */
static void vfs_fd_reset_for_test(void)
{
    int i;
    for (i = 0; i < VFS_MAX_OPEN_FILES; i++) open_files[i].in_use = 0;
}

/* fs/vfs.c の静的な登録表とマウント表を試験のたびに空にする
 * (実物を #include しているので直に触れる)。 */
static void reset(int has_stat)
{
    int i;

    drv_has_stat = has_stat;
    drv_stat_rc = VFS_OK;
    drv_stat_is_dir = 1;
    drv_list_rc = VFS_OK;
    drv_size_rc = VFS_OK;
    drv_size_calls = 0;
    drv_list_calls = 0;
    drv_write_rc = VFS_OK;
    drv_write_calls = 0;
    vfs_fd_reset_for_test();

    kmemset(&drv_ops_with_stat, 0, sizeof(drv_ops_with_stat));
    drv_ops_with_stat.name = "synth";
    drv_ops_with_stat.mount = drv_mount;
    drv_ops_with_stat.umount = drv_umount;
    drv_ops_with_stat.is_mounted = drv_is_mounted;
    drv_ops_with_stat.list_dir = drv_list_dir;
    drv_ops_with_stat.get_file_size = drv_get_file_size;
    drv_ops_with_stat.write_file = drv_write_file;
    drv_ops_with_stat.stat = drv_stat;

    drv_ops_no_stat = drv_ops_with_stat;
    drv_ops_no_stat.stat = 0;

    num_fs = 0;
    for (i = 0; i < VFS_MAX_FS; i++) kmemset(&mounts[i], 0, sizeof(mounts[i]));
    vfs_register_fs(has_stat ? &drv_ops_with_stat : &drv_ops_no_stat);
    if (vfs_mount("/synth", "hostdrv", "synth") != VFS_OK) {
        printf("  (harness) vfs_mount failed\n");
        exit(2);
    }
}

int main(void)
{
    printf("=== vfs_path_kind (票 H1 / 往復 3 の B6) ===\n");

    printf("== stat を持つドライバ: stat の答えが最終判断 ==\n");
    reset(1);
    drv_stat_is_dir = 1;
    check(vfs_path_kind("/synth/d") == VFS_KIND_DIR, "stat が DIR なら DIR");
    reset(1);
    drv_stat_is_dir = 0;
    check(vfs_path_kind("/synth/f") == VFS_KIND_FILE, "stat が FILE なら FILE");
    reset(1);
    drv_stat_rc = VFS_ERR_NOTFOUND;
    check(vfs_path_kind("/synth/x") == VFS_ERR_NOTFOUND,
          "stat が NOTFOUND ならそのまま (従来どおり)");

    printf("== B6 の反例: stat が読めず、列挙も読めない ==\n");
    reset(1);
    drv_stat_rc = VFS_ERR_IO;            /* Basic 問い合わせが一時的に失敗 */
    drv_list_rc = OS32_ERR_FULL;         /* 1000 件超 */
    check(vfs_path_kind("/synth/big") != VFS_KIND_FILE,
          "**ファイルと判定しない** (get_file_size はディレクトリでも成功する)");
    check(vfs_path_kind("/synth/big") == VFS_ERR_IO,
          "stat のエラーをそのまま伝える");
    check(drv_size_calls == 0, "get_file_size を呼ばない");
    check(drv_list_calls == 0, "stat を持つドライバではプローブへ落ちない");

    reset(1);
    drv_stat_rc = VFS_ERR_IO;
    drv_list_rc = OS32_ERR_IO;           /* 途中で切れた列挙 */
    check(vfs_path_kind("/synth/big") == VFS_ERR_IO,
          "途中で切れた列挙でもファイルと判定しない");

    reset(1);
    drv_stat_rc = VFS_ERR_INVAL;
    check(vfs_path_kind("/synth/x") == VFS_ERR_INVAL,
          "stat の他のエラーもそのまま伝える");

    printf("== stat を持たないドライバだけプローブする ==\n");
    reset(0);
    drv_list_rc = VFS_OK;
    check(vfs_path_kind("/synth/d") == VFS_KIND_DIR,
          "列挙が通ればディレクトリ (従来どおり)");
    check(drv_size_calls == 0, "その場合 get_file_size は呼ばない");

    reset(0);
    drv_list_rc = VFS_ERR_NOTDIR;
    check(vfs_path_kind("/synth/f") == VFS_KIND_FILE,
          "「ディレクトリではない」なら get_file_size へ進む");
    check(drv_size_calls == 1, "get_file_size を 1 回呼ぶ");

    reset(0);
    drv_list_rc = VFS_ERR_NOTFOUND;
    drv_size_rc = VFS_ERR_NOTFOUND;
    check(vfs_path_kind("/synth/x") == VFS_ERR_NOTFOUND,
          "どちらも無ければ NOTFOUND");

    reset(0);
    drv_list_rc = OS32_ERR_FULL;
    check(vfs_path_kind("/synth/big") == OS32_ERR_FULL,
          "列挙が FULL なら**そのまま伝える** (ファイルに化けない)");
    check(drv_size_calls == 0, "get_file_size を呼ばない");

    reset(0);
    drv_list_rc = OS32_ERR_IO;
    check(vfs_path_kind("/synth/big") == OS32_ERR_IO,
          "列挙が IO なら**そのまま伝える**");
    check(drv_size_calls == 0, "get_file_size を呼ばない");

    printf("== マウントルートと未マウント ==\n");
    reset(1);
    drv_stat_rc = VFS_ERR_IO;
    check(vfs_path_kind("/synth") == VFS_KIND_DIR,
          "マウントルートは常に DIR (ドライバに聞かない)");
    check(vfs_path_kind("/nowhere/x") == VFS_ERR_NOMOUNT,
          "マウントが無ければ NOMOUNT");

    printf("== B7: open の受け手が種別の不明をどう扱うか ==\n");
    /* 実物の vfs_open_internal を通す。戻り値が FD (>= 3) なら開けている。 */

    /* HostDrv 相当: stat が IO、列挙は成功、get_file_size はディレクトリでも
     * 成功する。B7 の反例そのもの。 */
    reset(1);
    drv_stat_rc = VFS_ERR_IO;
    drv_list_rc = VFS_OK;
    drv_size_rc = VFS_OK;
    check(vfs_open("/synth/d", O_RDONLY) < 0,
          "HostDrv 相当: stat が IO なら **FD を返さない**");
    check(vfs_open("/synth/d", O_RDONLY) == VFS_ERR_IO,
          "そのエラーを返す (ISDIR にも NOTFOUND にもしない)");
    check(drv_size_calls == 0, "get_file_size まで進まない");

    /* ext2 相当: stat の inode 読み出しだけが失敗し、後続は成功する */
    reset(1);
    drv_stat_rc = VFS_ERR_IO;
    drv_list_rc = VFS_ERR_NOTDIR;      /* 列挙は「ディレクトリでない」と答える */
    drv_size_rc = VFS_OK;
    check(vfs_open("/synth/etc", O_RDONLY) < 0,
          "ext2 相当: stat が IO なら FD を返さない (`cat /etc` を通さない)");
    check(drv_size_calls == 0, "get_file_size まで進まない");

    /* O_CREAT でも同じ — 不明なまま作らない */
    reset(1);
    drv_stat_rc = VFS_ERR_IO;
    check(vfs_open("/synth/d", O_WRONLY | O_CREAT) < 0,
          "O_CREAT でも種別が不明なら開かない");
    check(drv_write_calls == 0, "作成もしない");

    /* --- 正常系の回帰 --- */
    reset(1);
    drv_stat_is_dir = 0;
    check(vfs_open("/synth/f", O_RDONLY) >= 3, "通常ファイルは開ける");

    reset(1);
    drv_stat_is_dir = 1;
    check(vfs_open("/synth/d", O_RDONLY) == VFS_ERR_ISDIR,
          "DIR は従来どおり ISDIR");

    reset(1);
    drv_stat_rc = VFS_ERR_NOTFOUND;
    drv_size_rc = VFS_ERR_NOTFOUND;
    check(vfs_open("/synth/new", O_WRONLY | O_CREAT) >= 3,
          "不存在 + O_CREAT は作れる (NOTFOUND は続行する)");
    check(drv_write_calls == 1, "空ファイルを 1 度書いて作る");

    reset(1);
    drv_stat_rc = VFS_ERR_NOTFOUND;
    drv_size_rc = VFS_ERR_NOTFOUND;
    check(vfs_open("/synth/new", O_RDONLY) == VFS_ERR_NOTFOUND,
          "不存在 + O_CREAT 無しは NOTFOUND (従来どおり)");

    /* stat を持たないドライバ経由でも受け手は同じ */
    reset(0);
    drv_list_rc = OS32_ERR_FULL;
    check(vfs_open("/synth/big", O_RDONLY) == OS32_ERR_FULL,
          "stat 無しドライバでも、読めない列挙で FD を返さない");
    check(drv_size_calls == 0, "get_file_size まで進まない");

    reset(0);
    drv_list_rc = VFS_ERR_NOTDIR;
    drv_size_rc = VFS_OK;
    check(vfs_open("/synth/f", O_RDONLY) >= 3,
          "stat 無しドライバでも通常ファイルは開ける");

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
