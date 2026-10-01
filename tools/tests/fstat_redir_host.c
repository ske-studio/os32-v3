/* =========================================================================
 *  FSTAT_REDIR_HOST.C — `fstat` がリダイレクトに従い、`isatty` と一致する
 *
 *  票: docs/archive/test/TASK_FSTAT_REDIR.md の受入 F4 / F5
 *  実行: python3 -B tools/tests/test_fstat_redir.py [--target] [--mutate]
 *  記録: tools/tests/fstat_redir_tdd.md
 *
 *  実物の fs/vfs.c + fs/vfs_fd.c + **fs/fd_redirect.c** を同じ翻訳単位に
 *  取り込み、境界 (kstring / kmalloc / コンソール / キーボード) だけを同義の
 *  C で置く。FS ドライバは合成した VfsOps。
 *
 *  直す前の反例 (2026-09-17、ゲストのランナーが暴いた):
 *    $ stat_t                 → PASS 5/5   fd 1 は本当に端末なので両方正しい
 *    $ stat_t > /host/st.txt  → FAIL 4/5
 *        [OK]   fd=1 is a character device   ← fstat は無条件で S_IFCHR
 *        [FAIL] isatty(1) == 1               ← isatty はリダイレクトを見る
 *    **同じ fd について 2 つの API が食い違っていた。**
 *
 *  ここで押さえるのは 2 つ:
 *    F4  リダイレクトの有無で st_mode が変わる (ファイルなら実体を答える)
 *    F5  **fstat が S_IFCHR と言うこと ⇔ isatty が 1 を返すこと**。
 *        状態 (コンソール / ファイル / パイプ) × fd 0/1/2 の総当たりで見る。
 *        F4 だけなら「片方だけ直した版」が通ってしまう — F5 がそれを止める。
 *
 *  make・エミュレータ・実配備には一切触れない。
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

/* ---- fs/vfs_fd.c の境界 (コンソール / キーボード) --------------------------
 * リダイレクトは**贋物を置かない** — この票の主題そのものなので、
 * fs/fd_redirect.c を実物のまま下で取り込む。 */
static u32 con_written;
int kbd_getchar(void) { return '\n'; }
void console_write(const char *buf, u32 size, u8 color)
{ (void)buf; (void)color; con_written += size; }

#include "../../fs/vfs_fd.c"
#include "../../fs/fd_redirect.c"
#include "redir_access_stub.h"

/* fs/fd_redirect.c の書き込み時の再検査 (票 TASK_KAPI_OUTPUT_GUARD) が引く exec/exec.c
 * の 5 本。ホストではユーザ帯の番地は無いので ring3_ptr_ok は常に 0 (= 再検査を通らない)。 */
int ring3_ptr_ok(u32 p) { (void)p; return 0; }
int ring3_user_ranges_writable(u32 pa, u32 la, u32 pb, u32 lb)
{ (void)pa; (void)la; (void)pb; (void)lb; return 1; }
/* 由来つきの門 (2026-09-26): ここの呼び手は CPL=0 扱い (由来はアプリでない)。
 * アプリ由来の筋書きは tools/tests/ring3_guard_host.c が見る。 */
int ring3_user_ranges_writable_always(u32 pa, u32 la, u32 pb, u32 lb)
{ (void)pa; (void)la; (void)pb; (void)lb; return 1; }
int ring3_call_from_user(void) { return 0; }
void ring3_fault_kill(void) { for (;;) { } }

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

#define SYNTH_SIZE   4096u
#define SYNTH_MTIME  0x5EED0001u
#define SYNTH_ATIME  0x5EED0002u
#define SYNTH_CTIME  0x5EED0003u
#define SYNTH_INO    1234u

static int drv_stat(void *ctx, const char *path, OS32_Stat *buf)
{
    (void)ctx; (void)path;
    kmemset(buf, 0, sizeof(OS32_Stat));
    buf->st_ino = SYNTH_INO;
    buf->st_mode = (u16)(OS_S_IFREG | 0644);
    buf->st_nlink = 1;
    buf->st_size = SYNTH_SIZE;
    buf->st_atime = SYNTH_ATIME;
    buf->st_mtime = SYNTH_MTIME;
    buf->st_ctime = SYNTH_CTIME;
    return VFS_OK;
}

static int drv_list_dir(void *ctx, const char *path, vfs_dir_cb cb, void *uc)
{ (void)ctx; (void)path; (void)cb; (void)uc; return VFS_OK; }

static int drv_get_file_size(void *ctx, const char *path, u32 *size)
{ (void)ctx; (void)path; *size = SYNTH_SIZE; return VFS_OK; }

static int drv_write_file(void *ctx, const char *path, const void *d, u32 n)
{ (void)ctx; (void)path; (void)d; (void)n; return VFS_OK; }

static int drv_read_stream(void *ctx, const char *path, void *buf, u32 n, u32 off)
{ (void)ctx; (void)path; (void)off; kmemset(buf, 'x', n); return (int)n; }

static int drv_write_stream(void *ctx, const char *path, const void *d,
                            u32 n, u32 off)
{ (void)ctx; (void)path; (void)d; (void)off; return (int)n; }

static int drv_ctx_tag = 1;
static void *drv_mount(int dev) { (void)dev; return &drv_ctx_tag; }
static void drv_umount(void *ctx) { (void)ctx; }
static int drv_is_mounted(void *ctx) { (void)ctx; return 1; }

static VfsOps drv_ops;

static void reset(void)
{
    int i;

    con_written = 0;
    for (i = 0; i < VFS_MAX_OPEN_FILES; i++) open_files[i].in_use = 0;
    fd_redirect_init();

    kmemset(&drv_ops, 0, sizeof(drv_ops));
    drv_ops.name = "synth";
    drv_ops.mount = drv_mount;
    drv_ops.umount = drv_umount;
    drv_ops.is_mounted = drv_is_mounted;
    drv_ops.list_dir = drv_list_dir;
    drv_ops.get_file_size = drv_get_file_size;
    drv_ops.write_file = drv_write_file;
    drv_ops.stat = drv_stat;
    drv_ops.read_stream = drv_read_stream;
    drv_ops.write_stream = drv_write_stream;

    num_fs = 0;
    for (i = 0; i < VFS_MAX_FS; i++) kmemset(&mounts[i], 0, sizeof(mounts[i]));
    vfs_register_fs(&drv_ops);
    if (vfs_mount("/synth", "hostdrv", "synth") != VFS_OK) {
        printf("  (harness) vfs_mount failed\n");
        exit(2);
    }
}

static u16 ifmt_of(int fd)
{
    OS32_Stat st;
    kmemset(&st, 0xAB, sizeof(st));
    if (vfs_fstat(fd, &st) != VFS_OK) return 0;
    return (u16)(st.st_mode & OS_S_IFMT);
}

/* ========================================================================= */
/*  F4 — リダイレクトの有無で st_mode が変わる                                */
/* ========================================================================= */

static void case_f4(void)
{
    OS32_Stat st;
    OS32_Stat direct;
    int probe;

    printf("== F4: リダイレクトの有無で fstat の st_mode が変わる ==\n");

    /* (a) コンソールのまま — 従来どおりキャラクタデバイス */
    reset();
    check(vfs_fstat(1, &st) == VFS_OK, "コンソール: fstat(1) は成功する");
    check((st.st_mode & OS_S_IFMT) == OS_S_IFCHR,
          "コンソール: fd=1 は S_IFCHR");
    check(st.st_ino == 2u && st.st_nlink == 1, "コンソール: 従来の ino / nlink");
    check(st.st_size == 0u, "コンソール: 大きさは 0");

    /* (b) `> file` — **実体を答える**。無条件 S_IFCHR ではここが落ちる */
    reset();
    check(fd_redirect_to_file(1, "/synth/out", FD_REDIR_WRITE) == 0,
          "(準備) fd 1 をファイルへ向ける");
    check(vfs_fstat(1, &st) == VFS_OK, "> file: fstat(1) は成功する");
    check((st.st_mode & OS_S_IFMT) == OS_S_IFREG,
          "> file: fd=1 は S_IFREG (S_IFCHR ではない)");
    check((st.st_mode & OS_S_IFMT) != OS_S_IFCHR,
          "> file: キャラクタデバイスとは答えない");
    check(st.st_size == SYNTH_SIZE, "> file: 大きさは実ファイルのもの");
    check(st.st_mtime == SYNTH_MTIME && st.st_atime == SYNTH_ATIME &&
          st.st_ctime == SYNTH_CTIME, "> file: 時刻は実ファイルのもの");
    check((st.st_mode & 0777) == 0644, "> file: 権限も実ファイルのもの");
    check(st.st_ino == SYNTH_INO, "> file: inode も実ファイルのもの");

    /* (c) 同じファイルを直に開いた FD の fstat と**同じ値**であること。
     * 「それらしい値を作った」のではなく既存の経路を通っていることの確認。 */
    probe = vfs_open("/synth/out", O_RDONLY);
    check(probe >= 3, "(準備) 同じファイルを直に開く");
    check(vfs_fstat(probe, &direct) == VFS_OK, "(準備) 直の fstat");
    check(direct.st_mode == st.st_mode && direct.st_size == st.st_size &&
          direct.st_mtime == st.st_mtime && direct.st_dev == st.st_dev &&
          direct.st_ino == st.st_ino,
          "> file: 直に開いた FD の fstat と 1 バイトも違わない");
    vfs_close(probe);

    /* (d) 解除したら戻る */
    fd_redirect_reset(1);
    check(ifmt_of(1) == OS_S_IFCHR, "解除すると S_IFCHR に戻る");

    /* (e) 巻き添えにしない — 向けていない fd は変わらない */
    reset();
    check(fd_redirect_to_file(1, "/synth/out", FD_REDIR_WRITE) == 0,
          "(準備) fd 1 だけ向ける");
    check(ifmt_of(0) == OS_S_IFCHR, "fd 0 は巻き添えにならない");
    check(ifmt_of(2) == OS_S_IFCHR, "fd 2 は巻き添えにならない");
    check(ifmt_of(1) == OS_S_IFREG, "向けた fd 1 だけが S_IFREG");

    /* (f) `< file` (stdin) と `2> file` (stderr) も同じ道 */
    reset();
    check(fd_redirect_to_file(0, "/synth/in", FD_REDIR_READ) == 0,
          "(準備) fd 0 を読み込みで向ける");
    check(ifmt_of(0) == OS_S_IFREG, "< file: fd 0 も S_IFREG");
    reset();
    check(fd_redirect_to_file(2, "/synth/err", FD_REDIR_APPEND) == 0,
          "(準備) fd 2 を追記で向ける");
    check(ifmt_of(2) == OS_S_IFREG, "2>> file: fd 2 も S_IFREG");
}

/* ========================================================================= */
/*  F5 — fstat と isatty は必ず一致する                                       */
/*                                                                           */
/*  これが肝。F4 は fstat しか見ないので「片方だけ直した版」を通してしまう。   */
/*  「2 つの API が同じことを言う」を主張にすると、どちらを崩しても落ちる。    */
/* ========================================================================= */

static void agree(int fd, const char *where)
{
    OS32_Stat st;
    char name[160];
    int chr;
    int tty;

    if (vfs_fstat(fd, &st) != VFS_OK) {
        sprintf(name, "%s: fd=%d fstat が失敗した", where, fd);
        check(0, name);
        return;
    }
    chr = ((st.st_mode & OS_S_IFMT) == OS_S_IFCHR) ? 1 : 0;
    tty = (vfs_isatty(fd) == 1) ? 1 : 0;
    sprintf(name, "%s: fd=%d  fstat S_IFCHR=%d <-> isatty=%d", where, fd,
            chr, tty);
    check(chr == tty, name);
}

static void agree_all(const char *where)
{
    agree(0, where);
    agree(1, where);
    agree(2, where);
}

static void case_f5(void)
{
    printf("== F5: fstat が S_IFCHR ⇔ isatty が 1 (状態 x fd の総当たり) ==\n");

    reset();
    agree_all("コンソール");

    reset();
    fd_redirect_to_file(1, "/synth/out", FD_REDIR_WRITE);
    agree_all("> file");

    reset();
    fd_redirect_to_file(0, "/synth/in", FD_REDIR_READ);
    fd_redirect_to_file(1, "/synth/out", FD_REDIR_WRITE);
    fd_redirect_to_file(2, "/synth/err", FD_REDIR_WRITE);
    agree_all("0< 1> 2> すべて file");

    reset();
    {
        static u8 pipebuf[256];
        fd_redirect_to_buffer(1, pipebuf, sizeof(pipebuf), 0);
        agree_all("cmd | cmd (fd 1 がパイプ)");
        fd_redirect_reset(1);
        fd_redirect_to_buffer(0, pipebuf, sizeof(pipebuf), 7);
        agree_all("cmd | cmd (fd 0 がパイプ)");
    }

    /* 解除して元に戻ることも一致の対象 */
    reset();
    fd_redirect_to_file(1, "/synth/out", FD_REDIR_WRITE);
    fd_redirect_reset(1);
    agree_all("解除した後");
}

/* ========================================================================= */
/*  パイプ (FD_TARGET_BUFFER) の答え方                                        */
/*                                                                           */
/*  POSIX の S_IFIFO を足して (os32_kapi_shared.h、値も POSIX と同じ 0x1000)  */
/*  それを返す。S_IFCHR のままだと F5 の一致が崩れ、S_IFREG だと「seek でき   */
/*  る実体がある」と嘘になる。大きさは 0 — 残量を返すと「これだけ読める」と   */
/*  誤解される (POSIX でもパイプの st_size は未規定)。                        */
/* ========================================================================= */

static void case_pipe(void)
{
    static u8 pipebuf[256];
    OS32_Stat st;

    printf("== パイプ: S_IFIFO と答える (S_IFCHR でも S_IFREG でもない) ==\n");

    reset();
    check(fd_redirect_to_buffer(1, pipebuf, sizeof(pipebuf), 0) == 0,
          "(準備) fd 1 をパイプバッファへ向ける");
    check(vfs_fstat(1, &st) == VFS_OK, "パイプ: fstat(1) は成功する");
    check((st.st_mode & OS_S_IFMT) == OS_S_IFIFO, "パイプ: S_IFIFO");
    check((st.st_mode & OS_S_IFMT) != OS_S_IFCHR,
          "パイプ: キャラクタデバイスとは答えない");
    check((st.st_mode & OS_S_IFMT) != OS_S_IFREG,
          "パイプ: 通常ファイルとも答えない");
    check(vfs_isatty(1) == 0, "パイプ: isatty(1) == 0");
    check(st.st_size == 0u, "パイプ: 大きさは 0 (残量を大きさと言わない)");

    /* 書き込んだ後も大きさは 0 のまま (残量に化けない) */
    check(vfs_write_fd(1, "hello", 5) == 5, "(準備) パイプへ 5 バイト書く");
    check(fd_redirect_get_buf_len(1) == 5u, "(準備) バッファには 5 バイト");
    check(vfs_fstat(1, &st) == VFS_OK && st.st_size == 0u,
          "パイプ: 書いた後も大きさは 0");

    /* S_IFIFO は他の種別とビットが重ならない (st_mode の読み手が迷わない) */
    check(OS_S_IFIFO != OS_S_IFCHR && OS_S_IFIFO != OS_S_IFREG &&
          OS_S_IFIFO != OS_S_IFDIR && (OS_S_IFIFO & ~OS_S_IFMT) == 0,
          "S_IFIFO は S_IFMT の中で他とぶつからない");
}

/* ========================================================================= */
/*  回帰 — この票で変えていないところ (受入 F6)                               */
/* ========================================================================= */

static void case_regress(void)
{
    OS32_Stat st;
    int fd;

    printf("== 回帰: fd>=3 と引数検査は従来どおり ==\n");

    reset();
    fd = vfs_open("/synth/f", O_RDONLY);
    check(fd >= 3, "(準備) 普通に開く");
    check(vfs_fstat(fd, &st) == VFS_OK, "fd>=3: FS へ委ねる");
    check((st.st_mode & OS_S_IFMT) == OS_S_IFREG, "fd>=3: FS の答えが出る");
    check(st.st_size == SYNTH_SIZE, "fd>=3: 大きさも FS のもの");
    check(vfs_isatty(fd) == 0, "fd>=3: 端末ではない");
    vfs_close(fd);

    check(vfs_fstat(1, (OS32_Stat *)0) == VFS_ERR_INVAL,
          "buf が NULL なら INVAL");
    check(vfs_fstat(9, &st) == VFS_ERR_INVAL, "使っていない fd は INVAL");
    check(vfs_fstat(-1, &st) == VFS_ERR_INVAL, "負の fd は INVAL");
    check(vfs_isatty(9) == VFS_ERR_INVAL, "isatty も使っていない fd は INVAL");

    /* fd_is_redirected も同じ 1 か所から導いているので、種別と食い違わない */
    reset();
    check(fd_is_redirected(1) == 0 && ifmt_of(1) == OS_S_IFCHR,
          "fd_is_redirected(1)=0 と S_IFCHR が一致");
    fd_redirect_to_file(1, "/synth/out", FD_REDIR_WRITE);
    check(fd_is_redirected(1) == 1 && ifmt_of(1) != OS_S_IFCHR,
          "fd_is_redirected(1)=1 と 非 S_IFCHR が一致");
    check(fd_is_redirected(3) == 0, "fd 3 以降は「リダイレクト中」ではない");

    /* コンソールへの書き込みが生きている (リダイレクトを外した後) */
    reset();
    con_written = 0;
    check(vfs_write_fd(1, "abc", 3) == 3 && con_written == 3u,
          "コンソールへの write は従来どおり");
}

int main(void)
{
    printf("=== 票 TASK_FSTAT_REDIR: fstat がリダイレクトに従い isatty と"
           "一致する ===\n");
    case_f4();
    case_f5();
    case_pipe();
    case_regress();
    printf("\n%d 件中 %d 件 FAIL\n", checks, failures);
    return failures ? 1 : 0;
}
