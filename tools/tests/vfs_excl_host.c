/* =========================================================================
 *  VFS_EXCL_HOST.C — 排他的作成 O_EXCL (票 H2 §2-1、KAPI v53) を
 *                    **実物の fs/vfs.c + fs/vfs_fd.c** で確かめる
 *
 *  票:   docs/archive/shell/TASK_H2.md §2-1 / §4-1 の X1 X2
 *  実行: python3 -B tools/tests/test_vfs_excl.py [--target]
 *  記録: tools/tests/vfs_excl_tdd.md
 *
 *  vfs_kind_host.c と同じ作法: 実物の fs/vfs.c と fs/vfs_fd.c をそのまま
 *  #include し、境界 (kstring / kmalloc / コンソール) だけを同義の C で置く。
 *  FS ドライバは合成した VfsOps で、
 *    - create_excl を**持つ**もの (ext2 相当) と**持たない**もの (HostDrv 相当)
 *    - create_excl / get_file_size / write_file の戻り値と**呼び出し回数**
 *  を 1 つずつ指定できる。
 *
 *  ここで押さえる規則 (票 §2-1):
 *    X1  O_CREAT|O_EXCL で
 *          既存ファイル      -> VFS_ERR_EXIST
 *          既存ディレクトリ  -> **ISDIR ではなく** VFS_ERR_EXIST
 *          判定不能 (I/O)    -> **その負値**をそのまま返す (票 B8)
 *        いずれも**作らない・切り詰めない** (write_file の呼び出し 0 回)
 *    X2  O_EXCL 単独 (O_CREAT 無し)          -> VFS_ERR_INVAL
 *        create_excl を持たない FS           -> VFS_ERR_NOSYS
 *        **非対応の判定は種別検査より先**    -> get_file_size / list_dir を
 *                                               1 度も呼ばない (往復 2 の指摘)
 *
 *  エミュレータ・実配備・make には一切触れない。
 *  [C1] C89 / GNU89。宣言はブロック先頭、`//` コメント無し。
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
int kbd_getchar_kapi(void) { return '\n'; }
void console_write(const char *buf, u32 size, u8 color)
{ (void)buf; (void)size; (void)color; }
int res_owner_get(void) { return 0; }

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

/* ---- 合成 FS ドライバ -------------------------------------------------- */

static int drv_excl_rc;          /* create_excl の戻り値 */
static int drv_excl_calls;
static int drv_stat_rc;
static int drv_stat_is_dir;
static int drv_size_rc;
static int drv_size_calls;
static int drv_list_calls;
static int drv_write_rc;
static int drv_write_calls;

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
    return VFS_OK;
}

static int drv_get_file_size(void *ctx, const char *path, u32 *size)
{
    (void)ctx; (void)path;
    drv_size_calls++;
    if (drv_size_rc != VFS_OK) return drv_size_rc;
    *size = 4096;
    return VFS_OK;
}

static int drv_write_file(void *ctx, const char *path, const void *data, u32 size)
{
    (void)ctx; (void)path; (void)data; (void)size;
    drv_write_calls++;
    return drv_write_rc;
}

/* ext2 の ext2_vfs_create_excl と同じ 3 値の契約を模す。
 * **存在の判定は呼び手 (試験) が仕込んだ値そのまま** — 「読めなかった」を
 * 「無い」に読み替える実装だと X1 の 3 つ目が落ちる。 */
static int drv_create_excl(void *ctx, const char *path)
{
    (void)ctx; (void)path;
    drv_excl_calls++;
    return drv_excl_rc;
}

static int drv_ctx_tag = 1;
static void *drv_mount(int dev) { (void)dev; return &drv_ctx_tag; }
static void drv_umount(void *ctx) { (void)ctx; }
static int drv_is_mounted(void *ctx) { (void)ctx; return 1; }

static VfsOps drv_ops_excl;      /* create_excl を持つ (ext2 相当) */
static VfsOps drv_ops_noexcl;    /* 持たない (HostDrv / FAT / ISO9660 相当) */

static void vfs_fd_reset_for_test(void)
{
    int i;
    for (i = 0; i < VFS_MAX_OPEN_FILES; i++) open_files[i].in_use = 0;
}

static void reset(int has_excl)
{
    int i;

    drv_excl_rc = VFS_OK;
    drv_excl_calls = 0;
    drv_stat_rc = VFS_OK;
    drv_stat_is_dir = 0;
    drv_size_rc = VFS_OK;
    drv_size_calls = 0;
    drv_list_calls = 0;
    drv_write_rc = VFS_OK;
    drv_write_calls = 0;
    vfs_fd_reset_for_test();

    kmemset(&drv_ops_excl, 0, sizeof(drv_ops_excl));
    drv_ops_excl.name = "synth";
    drv_ops_excl.mount = drv_mount;
    drv_ops_excl.umount = drv_umount;
    drv_ops_excl.is_mounted = drv_is_mounted;
    drv_ops_excl.list_dir = drv_list_dir;
    drv_ops_excl.get_file_size = drv_get_file_size;
    drv_ops_excl.write_file = drv_write_file;
    drv_ops_excl.stat = drv_stat;
    drv_ops_excl.create_excl = drv_create_excl;

    drv_ops_noexcl = drv_ops_excl;
    drv_ops_noexcl.create_excl = 0;

    num_fs = 0;
    for (i = 0; i < VFS_MAX_FS; i++) kmemset(&mounts[i], 0, sizeof(mounts[i]));
    vfs_register_fs(has_excl ? &drv_ops_excl : &drv_ops_noexcl);
    if (vfs_mount("/synth", "hostdrv", "synth") != VFS_OK) {
        printf("  (harness) vfs_mount failed\n");
        exit(2);
    }
}

/* ========================================================================= */
/*  X1 — O_CREAT|O_EXCL の 3 値                                              */
/* ========================================================================= */

static void case_x1(void)
{
    int rc;

    printf("== X1: O_CREAT|O_EXCL は既存を EXIST、判定不能はその負値 ==\n");

    /* (a) 既存の通常ファイル */
    reset(1);
    drv_excl_rc = VFS_ERR_EXIST;
    rc = vfs_open("/synth/f", O_WRONLY | O_CREAT | O_EXCL);
    check(rc == VFS_ERR_EXIST, "既存ファイル -> VFS_ERR_EXIST");
    check(drv_write_calls == 0, "既存ファイル: 作らない・切り詰めない");
    check(drv_excl_calls == 1, "create_excl は 1 度だけ呼ばれる");

    /* (b) 既存のディレクトリ。**ISDIR ではなく EXIST** (票 §2-1)。
     * 「ディレクトリだから ISDIR」にすると、呼び手 (hsync) が
     * 「予約名が在る」ことと「別の何かが在る」ことを区別できなくなる。 */
    reset(1);
    drv_stat_is_dir = 1;
    drv_excl_rc = VFS_ERR_EXIST;
    rc = vfs_open("/synth/d", O_WRONLY | O_CREAT | O_EXCL);
    check(rc == VFS_ERR_EXIST, "既存ディレクトリ -> ISDIR ではなく EXIST");
    check(drv_write_calls == 0, "既存ディレクトリ: 1 バイトも書かない");

    /* (c) 判定不能 (I/O エラー)。**「無い」と読み替えて作らない** (票 B8) */
    reset(1);
    drv_excl_rc = VFS_ERR_IO;
    rc = vfs_open("/synth/x", O_WRONLY | O_CREAT | O_EXCL);
    check(rc == VFS_ERR_IO, "判定不能 (IO) -> その負値をそのまま返す");
    check(drv_write_calls == 0, "判定不能: 作らない");

    reset(1);
    drv_excl_rc = VFS_ERR_ROFS;
    check(vfs_open("/synth/x", O_WRONLY | O_CREAT | O_EXCL) == VFS_ERR_ROFS,
          "書き込み禁止 (ROFS) もそのまま返す");

    reset(1);
    drv_excl_rc = VFS_ERR_NOSPC;
    check(vfs_open("/synth/x", O_WRONLY | O_CREAT | O_EXCL) == VFS_ERR_NOSPC,
          "空き不足 (NOSPC) もそのまま返す");

    /* (d) 作れた場合は FD が出て、長さ 0 で始まる */
    reset(1);
    drv_excl_rc = VFS_OK;
    rc = vfs_open("/synth/new", O_WRONLY | O_CREAT | O_EXCL);
    check(rc >= 3, "作れたら FD が出る");
    check(rc >= 3 && vfs_get_size(rc) == 0, "作りたての長さは 0");
    check(drv_write_calls == 0,
          "**write_file 経由の作成は通らない** (create_excl だけが作る)");
    check(drv_size_calls == 0, "get_file_size を通らない (作ったばかり)");

    /* (e) O_TRUNC が付いていても create_excl が作った長さ 0 を切り詰めない */
    reset(1);
    drv_excl_rc = VFS_OK;
    rc = vfs_open("/synth/new", O_WRONLY | O_CREAT | O_EXCL | O_TRUNC);
    check(rc >= 3, "O_TRUNC 併用でも作れる");
    check(drv_write_calls == 0, "O_TRUNC 併用でも余計な書き込みをしない");
}

/* ========================================================================= */
/*  X2 — O_EXCL 単独 / 非対応 FS                                             */
/* ========================================================================= */

static void case_x2(void)
{
    int rc;

    printf("== X2: O_EXCL 単独は INVAL、非対応 FS は NOSYS ==\n");

    /* O_CREAT が無い O_EXCL は意味を成さない */
    reset(1);
    rc = vfs_open("/synth/f", O_WRONLY | O_EXCL);
    check(rc == VFS_ERR_INVAL, "O_EXCL 単独 (O_CREAT 無し) -> VFS_ERR_INVAL");
    check(drv_excl_calls == 0, "O_EXCL 単独: create_excl を呼ばない");
    check(drv_write_calls == 0, "O_EXCL 単独: 1 バイトも書かない");

    reset(1);
    check(vfs_open("/synth/f", O_RDONLY | O_EXCL) == VFS_ERR_INVAL,
          "O_RDONLY|O_EXCL も INVAL");

    /* create_excl を持たない FS。**「持っていない」と「できなかった」は別** */
    reset(0);
    rc = vfs_open("/synth/f", O_WRONLY | O_CREAT | O_EXCL);
    check(rc == VFS_ERR_NOSYS, "非対応 FS -> VFS_ERR_NOSYS");
    check(drv_write_calls == 0, "非対応 FS: 作らない");

    /* **非対応の判定は種別検査より先** (票 §6 往復 2 の指摘)。
     * 後ろに置くと、存在しない / 読めない宛先で NOTFOUND や IO が先に返り、
     * 呼び手が「この FS は O_EXCL を持たない」と気づけない。 */
    reset(0);
    drv_stat_is_dir = 1;
    check(vfs_open("/synth/d", O_WRONLY | O_CREAT | O_EXCL) == VFS_ERR_NOSYS,
          "非対応 FS: ディレクトリでも ISDIR ではなく NOSYS");
    check(drv_size_calls == 0 && drv_list_calls == 0,
          "非対応 FS: 種別検査 (get_file_size / list_dir) まで進まない");

    reset(0);
    drv_stat_rc = VFS_ERR_IO;
    drv_size_rc = VFS_ERR_IO;
    check(vfs_open("/synth/x", O_WRONLY | O_CREAT | O_EXCL) == VFS_ERR_NOSYS,
          "非対応 FS: stat が I/O で落ちても NOSYS が先に返る");
    check(drv_size_calls == 0, "非対応 FS: サイズ取得まで進まない");

    /* マウントが無い経路は従来どおり NOMOUNT (O_EXCL の判定より前) */
    reset(1);
    check(vfs_open("/nowhere/x", O_WRONLY | O_CREAT | O_EXCL) == VFS_ERR_NOMOUNT,
          "マウント外は NOMOUNT (経路の解決が先)");
}

/* ========================================================================= */
/*  既存の open が変わっていないこと (回帰)                                   */
/* ========================================================================= */

static void case_regression(void)
{
    int rc;

    printf("== 回帰: O_EXCL 無しの open は従来どおり ==\n");

    reset(1);
    drv_stat_is_dir = 1;
    check(vfs_open("/synth/d", O_RDONLY) == VFS_ERR_ISDIR,
          "ディレクトリは ISDIR (O_EXCL 無し)");

    reset(1);
    drv_stat_rc = VFS_ERR_NOTFOUND;
    drv_size_rc = VFS_ERR_NOTFOUND;
    rc = vfs_open("/synth/new", O_WRONLY | O_CREAT);
    check(rc >= 3, "不存在 + O_CREAT は作れる");
    check(drv_write_calls == 1, "従来経路は write_file で作る");
    check(drv_excl_calls == 0, "O_EXCL 無しなら create_excl を呼ばない");

    reset(1);
    drv_stat_rc = VFS_ERR_NOTFOUND;
    drv_size_rc = VFS_ERR_IO;
    check(vfs_open("/synth/x", O_WRONLY | O_CREAT) == VFS_ERR_IO,
          "サイズ取得の I/O 失敗を「無い」と読み替えない (票 B8)");
    check(drv_write_calls == 0, "その場合は作らない");

    reset(1);
    drv_size_rc = VFS_OK;
    rc = vfs_open("/synth/f", O_WRONLY | O_TRUNC);
    check(rc >= 3 && drv_write_calls == 1, "O_TRUNC は従来どおり切り詰める");
}

int main(void)
{
    printf("=== 票 H2 §2-1: 排他的作成 O_EXCL (KAPI v53) ===\n");
    case_x1();
    case_x2();
    case_regression();
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
