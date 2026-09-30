/* S3-K: FAT の stat が「そのボリュームに存在しえない名前」を NOTFOUND と
 * 言えるかの回帰試験 (2026-09-13)。列挙のエラー伝播 (S3I2-K) と、一括
 * 書き込みの f_close の失敗 (起動ログ、2026-09-25) もここで見る。
 *
 * 実物の fs/fatfs_vfs.c をそのまま取り込み、境界 (FatFs の f_*、Device /
 * IDE、kmalloc、kprintf) だけを贋物に差し替える。実デバイス・実イメージ・
 * 実 FatFs には一切触れない。
 *
 * 追う事象: FDD ブート (root = FAT、FF_USE_LFN 0) で KAPI v50 の
 * db_open_existing("/etc/settings.db", 0) が SQLITE_IOERR。v50 は hot
 * journal の有無を vfs_stat("<path>-journal") で見て NOTFOUND 以外を IOERR
 * とするが、"settings.db-journal" は 8.3 に収まらないので f_stat が
 * FR_INVALID_NAME を返していた。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fatfs/ff.h"
#include "vfs.h"
#include "ide.h"
#include "dev.h"
#include "os_time.h"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #x); exit(1); \
} } while (0)

/* ---- FatFs の境界 ----
 * f_stat / f_open は「次に返す FRESULT」を持ち、渡された FatFs パスを
 * 控える。他は呼ばれない前提で FR_INT_ERR を返す。 */
static FRESULT stub_stat_rc = FR_OK;
static FILINFO stub_stat_info;
static char    stub_last_path[FF_MAX_LFN + 32];
static int     stub_stat_calls;
static FRESULT stub_open_rc = FR_OK;
/* 書き込みの境界 (起動ログ、2026-09-25): f_write / f_close の戻りと回数。
 * stub_write_bw < 0 なら btw をそのまま bw に (全部書けた)。 */
static FRESULT stub_write_rc = FR_OK;
static int     stub_write_bw = -1;
static int     stub_write_calls;
static FRESULT stub_close_rc = FR_OK;
static int     stub_close_calls;
static int     stub_unlink_calls;
static int     stub_rename_calls;

static void stub_record(const char *path)
{
    size_t n = strlen(path);
    if (n >= sizeof(stub_last_path)) n = sizeof(stub_last_path) - 1;
    memcpy(stub_last_path, path, n);
    stub_last_path[n] = '\0';
}

FRESULT f_stat(const TCHAR *path, FILINFO *fno)
{
    stub_stat_calls++;
    stub_record(path);
    if (stub_stat_rc != FR_OK) return stub_stat_rc;
    if (fno) *fno = stub_stat_info;
    return FR_OK;
}

FRESULT f_open(FIL *fp, const TCHAR *path, BYTE mode)
{
    (void)mode;
    stub_record(path);
    if (fp) memset(fp, 0, sizeof(*fp));
    return stub_open_rc;
}

FRESULT f_close(FIL *fp) { (void)fp; stub_close_calls++; return stub_close_rc; }
FRESULT f_read(FIL *fp, void *buff, UINT btr, UINT *br)
{ (void)fp; (void)buff; (void)btr; if (br) *br = 0; return FR_OK; }
FRESULT f_write(FIL *fp, const void *buff, UINT btw, UINT *bw)
{
    (void)fp; (void)buff;
    stub_write_calls++;
    if (bw) *bw = stub_write_bw < 0 ? btw : (UINT)stub_write_bw;
    return stub_write_rc;
}
FRESULT f_lseek(FIL *fp, FSIZE_t ofs) { (void)fp; (void)ofs; return FR_OK; }
FRESULT f_unlink(const TCHAR *path) { stub_unlink_calls++; stub_record(path); return stub_open_rc; }
FRESULT f_mkdir(const TCHAR *path) { stub_record(path); return stub_open_rc; }
FRESULT f_rename(const TCHAR *a, const TCHAR *b)
{ (void)b; stub_rename_calls++; stub_record(a); return stub_open_rc; }

/* ---- S3I2-K: 列挙の境界 ----
 * f_readdir は「台本」を 1 段ずつ返す。rc != FR_OK でその段は失敗、
 * rc == FR_OK で name が空なら終端。opendir / closedir は回数を数える。 */
#define STUB_RD_MAX 8
struct stub_rd_step {
    FRESULT rc;
    const char *name;
    BYTE  attrib;
    DWORD fsize;
};
static struct stub_rd_step stub_rd_script[STUB_RD_MAX];
static int stub_rd_len;
static int stub_readdir_calls;
static int stub_opendir_calls;
static int stub_closedir_calls;

/* 列挙の callback が受け取ったものを控える。 */
#define SEEN_MAX 8
static VfsDirEntry seen[SEEN_MAX];
static int seen_count;

FRESULT f_opendir(DIR *dp, const TCHAR *path)
{
    stub_opendir_calls++;
    (void)dp;
    stub_record(path);
    return stub_open_rc;
}

FRESULT f_closedir(DIR *dp) { stub_closedir_calls++; (void)dp; return FR_OK; }

FRESULT f_readdir(DIR *dp, FILINFO *fno)
{
    const struct stub_rd_step *st;
    size_t n;
    (void)dp;
    if (stub_readdir_calls >= stub_rd_len) {
        /* 台本を使い切ったら終端 (fname 空) を返し続ける。 */
        stub_readdir_calls++;
        if (fno) fno->fname[0] = '\0';
        return FR_OK;
    }
    st = &stub_rd_script[stub_readdir_calls++];
    if (st->rc != FR_OK) return st->rc;
    if (!fno) return FR_OK;
    memset(fno, 0, sizeof(*fno));
    n = st->name ? strlen(st->name) : 0;
    if (n >= sizeof(fno->fname)) n = sizeof(fno->fname) - 1;
    if (n) memcpy(fno->fname, st->name, n);
    fno->fname[n] = '\0';
    fno->fattrib = st->attrib;
    fno->fsize = st->fsize;
    return FR_OK;
}
FRESULT f_getfree(const TCHAR *path, DWORD *nclst, FATFS **fatfs)
{ (void)path; if (nclst) *nclst = 0; if (fatfs) *fatfs = 0; return FR_INT_ERR; }
FRESULT f_mount(FATFS *fs, const TCHAR *path, BYTE opt)
{ (void)fs; (void)path; (void)opt; return FR_NOT_READY; }

/* ---- diskio / Device / IDE の境界 (mount 経路だけが使う) ---- */
void diskio_set_fdd_drive(int drv) { (void)drv; }
void diskio_set_hdd_drive(int drv) { (void)drv; }
void diskio_set_hdd_partition(u32 offset) { (void)offset; }
void diskio_set_hdd_sector_size(u16 sz) { (void)sz; }
void diskio_set_hdd_ide_phys_size(u16 sz) { (void)sz; }

int ide_get_info(int drive, IdeInfo *info)
{ (void)drive; (void)info; return -1; }
Device *dev_find(const char *name) { (void)name; return (Device *)0; }
int dev_blk_read_lba(Device *dev, u32 lba, int count, void *buf)
{ (void)dev; (void)lba; (void)count; (void)buf; return -1; }

os_time_t dos_time_to_epoch(u16 dos_date, u16 dos_time)
{ return (os_time_t)dos_date * 65536 + dos_time; }

void vfs_register_fs(VfsOps *ops) { (void)ops; }

/* ---- kernel の境界 ---- */
void *kzalloc(u32 size) { return calloc(1, size); }
void kfree(void *p) { free(p); }
void kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }

void *kmemset(void *dst, int val, u32 n) { return memset(dst, val, n); }
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

#include "../../drivers/pc98pt.c"   /* fatfs_vfs.c の PC98PartEntry 読み (票 TASK_HDD_INSTALL 段 1-4) */
/* 区画表の幾何 (fatfs_vfs.c が ext2 と同じ規則で引く、m4)。この試験は区画を
 * 探さないので、呼ばれたら IDENTIFY と同じ 8/17 を返すだけ */
int bootinfo_part_geom(int ide_drive, u16 *heads, u16 *spt)
{
    (void)ide_drive;
    if (heads) *heads = 8;
    if (spt) *spt = 17;
    return 2;
}
#include "../../fs/fatfs_vfs.c"

/* 起動ログの手順 (kernel/bootlog.c) を実物の fatfs_vfs_write に結ぶ。
 * 錠はホストでは空 (回数は tools/tests/bootlog_host.c が見る)。 */
#include "../../kernel/bootlog.c"
unsigned int bootlog_lock(void) { return 0; }
void bootlog_unlock(unsigned int f) { (void)f; }

/* ---- 試験用コンテキスト (mount を通さず直接組む) ---- */
static FatFsCtx test_ctx;

static void reset(void)
{
    memset(&test_ctx, 0, sizeof(test_ctx));
    test_ctx.pdrv = 0;          /* FDD */
    test_ctx.vol[0] = '0';
    test_ctx.vol[1] = ':';
    test_ctx.vol[2] = '\0';

    memset(&stub_stat_info, 0, sizeof(stub_stat_info));
    stub_stat_rc = FR_OK;
    stub_open_rc = FR_OK;
    stub_stat_calls = 0;
    stub_last_path[0] = '\0';
    stub_write_rc = FR_OK;
    stub_write_bw = -1;
    stub_write_calls = 0;
    stub_close_rc = FR_OK;
    stub_close_calls = 0;
    stub_unlink_calls = 0;
    stub_rename_calls = 0;

    memset(stub_rd_script, 0, sizeof(stub_rd_script));
    stub_rd_len = 0;
    stub_readdir_calls = 0;
    stub_opendir_calls = 0;
    stub_closedir_calls = 0;
    memset(&seen, 0, sizeof(seen));
    seen_count = 0;
}

static void collect_cb(const VfsDirEntry *e, void *ctx)
{
    (void)ctx;
    if (seen_count < SEEN_MAX) seen[seen_count] = *e;
    seen_count++;
}

static void script(int i, FRESULT rc, const char *name, BYTE attrib, DWORD sz)
{
    stub_rd_script[i].rc = rc;
    stub_rd_script[i].name = name;
    stub_rd_script[i].attrib = attrib;
    stub_rd_script[i].fsize = sz;
    if (i + 1 > stub_rd_len) stub_rd_len = i + 1;
}

/* ケース 1: 8.3 に収まらない名前の stat は「存在しない」。
 * FF_USE_LFN 0 の FatFs は f_stat に FR_INVALID_NAME を返す。 */
static int case_stat_invalid_name_is_notfound(void)
{
    OS32_Stat st;
    reset();
    stub_stat_rc = FR_INVALID_NAME;
    memset(&st, 0xAA, sizeof(st));
    CHECK(fatfs_vfs_stat(&test_ctx, "/etc/settings.db-journal", &st)
          == VFS_ERR_NOTFOUND);
    CHECK(stub_stat_calls == 1);
    CHECK(strcmp(stub_last_path, "0:/etc/settings.db-journal") == 0);
    return 0;
}

/* ケース 2: 素直な不存在も NOTFOUND (FR_NO_FILE / FR_NO_PATH)。 */
static int case_stat_missing_is_notfound(void)
{
    OS32_Stat st;
    reset();
    stub_stat_rc = FR_NO_FILE;
    CHECK(fatfs_vfs_stat(&test_ctx, "/etc/nosuch", &st) == VFS_ERR_NOTFOUND);
    reset();
    stub_stat_rc = FR_NO_PATH;
    CHECK(fatfs_vfs_stat(&test_ctx, "/nodir/x", &st) == VFS_ERR_NOTFOUND);
    return 0;
}

/* ケース 3: I/O 障害は NOTFOUND に化けない (v50 が IOERR と言うべき側)。 */
static int case_stat_disk_err_is_io(void)
{
    OS32_Stat st;
    reset();
    stub_stat_rc = FR_DISK_ERR;
    CHECK(fatfs_vfs_stat(&test_ctx, "/etc/settings.db", &st) == VFS_ERR_IO);
    reset();
    stub_stat_rc = FR_NOT_READY;
    CHECK(fatfs_vfs_stat(&test_ctx, "/etc/settings.db", &st) == VFS_ERR_IO);
    reset();
    stub_stat_rc = FR_NO_FILESYSTEM;
    CHECK(fatfs_vfs_stat(&test_ctx, "/etc/settings.db", &st) == VFS_ERR_IO);
    return 0;
}

/* ケース 4: 正常時の st_size / st_mode。 */
static int case_stat_ok_fills_size_mode(void)
{
    OS32_Stat st;

    reset();
    stub_stat_info.fsize = 4096;
    stub_stat_info.fattrib = 0;
    stub_stat_info.fdate = 0;
    stub_stat_info.ftime = 0;
    CHECK(fatfs_vfs_stat(&test_ctx, "/etc/settings.db", &st) == VFS_OK);
    CHECK(st.st_size == 4096);
    CHECK(st.st_mode == 0100644);

    reset();
    stub_stat_info.fsize = 0;
    stub_stat_info.fattrib = AM_DIR;
    CHECK(fatfs_vfs_stat(&test_ctx, "/etc", &st) == VFS_OK);
    CHECK(st.st_size == 0);
    CHECK(st.st_mode == 0040755);

    reset();
    stub_stat_info.fsize = 12;
    stub_stat_info.fattrib = AM_RDO;
    CHECK(fatfs_vfs_stat(&test_ctx, "/etc/ro.txt", &st) == VFS_OK);
    CHECK(st.st_size == 12);
    CHECK(st.st_mode == 0100444);

    /* buf が NULL の呼びは呼び手の誤りなので INVAL のまま。 */
    reset();
    CHECK(fatfs_vfs_stat(&test_ctx, "/etc/settings.db", (OS32_Stat *)0)
          == VFS_ERR_INVAL);
    return 0;
}

/* ケース 5: get_size も存在確認の入口なので同じ写像。 */
static int case_get_size_invalid_name_is_notfound(void)
{
    u32 size = 0xDEADBEEF;
    reset();
    stub_stat_rc = FR_INVALID_NAME;
    CHECK(fatfs_vfs_get_size(&test_ctx, "/etc/settings.db-journal", &size)
          == VFS_ERR_NOTFOUND);
    reset();
    stub_stat_rc = FR_DISK_ERR;
    CHECK(fatfs_vfs_get_size(&test_ctx, "/etc/settings.db", &size)
          == VFS_ERR_IO);
    return 0;
}

/* ケース 6: open / read / write / unlink 系の INVAL は変えない。
 * 「呼び手が不正な名前を渡した」診断はそのまま残す。 */
static int case_open_paths_keep_inval(void)
{
    char buf[16];
    reset();
    stub_open_rc = FR_INVALID_NAME;
    CHECK(fatfs_vfs_read(&test_ctx, "/etc/settings.db-journal", buf,
                         sizeof(buf)) == VFS_ERR_INVAL);
    CHECK(fatfs_vfs_write(&test_ctx, "/etc/settings.db-journal", buf,
                          sizeof(buf)) == VFS_ERR_INVAL);
    CHECK(fatfs_vfs_read_stream(&test_ctx, "/etc/settings.db-journal", buf,
                                sizeof(buf), 0) == VFS_ERR_INVAL);
    CHECK(fatfs_vfs_write_stream(&test_ctx, "/etc/settings.db-journal", buf,
                                 sizeof(buf), 0) == VFS_ERR_INVAL);
    CHECK(fatfs_vfs_unlink(&test_ctx, "/etc/settings.db-journal")
          == VFS_ERR_INVAL);
    CHECK(fatfs_vfs_mkdir(&test_ctx, "/etc/verylongdirname")
          == VFS_ERR_INVAL);
    CHECK(fatfs_vfs_rename(&test_ctx, "/a", "/etc/settings.db-journal")
          == VFS_ERR_INVAL);
    CHECK(fatfs_vfs_list(&test_ctx, "/etc/verylongdirname",
                         (vfs_dir_cb)0, (void *)0) == VFS_ERR_INVAL);
    return 0;
}

/* ケース 7: v50 の hot journal 検査そのものの再現。
 * 「本体は在る / journal は 8.3 に収まらない」で、v50 が IOERR ではなく
 * 「journal は無い」と判断できること。 */
static int case_v50_journal_probe_on_8_3(void)
{
    OS32_Stat st;
    int rc;

    reset();
    stub_stat_info.fsize = 8192;
    stub_stat_info.fattrib = 0;
    rc = fatfs_vfs_stat(&test_ctx, "/etc/settings.db", &st);
    CHECK(rc == VFS_OK);
    CHECK(st.st_size == 8192);

    /* 同じボリュームで journal 名を問う。8.3 に収まらないので FatFs は
     * FR_INVALID_NAME。v50 は NOTFOUND なら「無い」と読んで先へ進む。 */
    stub_stat_rc = FR_INVALID_NAME;
    rc = fatfs_vfs_stat(&test_ctx, "/etc/settings.db-journal", &st);
    CHECK(rc == VFS_ERR_NOTFOUND);
    CHECK(rc != VFS_ERR_INVAL);
    return 0;
}

/* ======== S3I2-K: fatfs_vfs_list の列挙エラー伝播 ======== */

/* ケース 8: 正常列挙 — 台本の全項目が callback に届き、戻り値は VFS_OK。
 * f_opendir / f_closedir は 1 回ずつ。 */
static int case_list_ok_enumerates_all(void)
{
    reset();
    script(0, FR_OK, "AUTOEXEC.BAT", 0, 512);
    script(1, FR_OK, "SYS", AM_DIR, 0);
    script(2, FR_OK, "VMKRNL.LZ4", 0, 481280u);
    script(3, FR_OK, "", 0, 0);          /* 終端 */

    CHECK(fatfs_vfs_list(&test_ctx, "/", collect_cb, (void *)0) == VFS_OK);
    CHECK(strcmp(stub_last_path, "0:/") == 0);
    CHECK(seen_count == 3);
    CHECK(strcmp(seen[0].name, "AUTOEXEC.BAT") == 0);
    CHECK(seen[0].type == VFS_TYPE_FILE);
    CHECK(seen[0].size == 512);
    CHECK(strcmp(seen[1].name, "SYS") == 0);
    CHECK(seen[1].type == VFS_TYPE_DIR);
    CHECK(strcmp(seen[2].name, "VMKRNL.LZ4") == 0);
    CHECK(seen[2].size == 481280u);
    CHECK(stub_opendir_calls == 1);
    CHECK(stub_closedir_calls == 1);
    return 0;
}

/* ケース 9: 2 件目の f_readdir が FR_DISK_ERR。
 * それまでに渡した 1 件は取り消さない (部分列挙) が、戻り値は負 (VFS_ERR_IO)。
 * 直す前はここで VFS_OK が返り、install の copy_directory が
 * 「欠けたファイルのまま成功」になっていた。f_closedir は 1 回。 */
static int case_list_readdir_error_propagates(void)
{
    reset();
    script(0, FR_OK, "SHELL.BIN", 0, 4096);
    script(1, FR_DISK_ERR, (const char *)0, 0, 0);
    script(2, FR_OK, "NEVER.BIN", 0, 1);   /* 打ち切りで届かない */

    CHECK(fatfs_vfs_list(&test_ctx, "/sys", collect_cb, (void *)0)
          == VFS_ERR_IO);
    CHECK(seen_count == 1);
    CHECK(strcmp(seen[0].name, "SHELL.BIN") == 0);
    CHECK(stub_readdir_calls == 2);        /* 3 段目は読まない */
    CHECK(stub_closedir_calls == 1);       /* 打ち切っても閉じる */

    /* 1 件目でいきなり失敗した場合は callback が 1 度も呼ばれない。 */
    reset();
    script(0, FR_NOT_READY, (const char *)0, 0, 0);
    CHECK(fatfs_vfs_list(&test_ctx, "/sys", collect_cb, (void *)0)
          == VFS_ERR_IO);
    CHECK(seen_count == 0);
    CHECK(stub_closedir_calls == 1);
    return 0;
}

/* ケース 10: f_opendir の失敗はそのまま負で返る (既存の挙動を固定)。
 * 開けていないので f_closedir は呼ばない — 二重解放にあたる呼びを
 * 足していないことを見張る。 */
static int case_list_opendir_error_propagates(void)
{
    reset();
    stub_open_rc = FR_NOT_READY;
    script(0, FR_OK, "SHELL.BIN", 0, 4096);

    CHECK(fatfs_vfs_list(&test_ctx, "/sys", collect_cb, (void *)0)
          == VFS_ERR_IO);
    CHECK(seen_count == 0);
    CHECK(stub_opendir_calls == 1);
    CHECK(stub_readdir_calls == 0);
    CHECK(stub_closedir_calls == 0);

    reset();
    stub_open_rc = FR_NO_PATH;
    CHECK(fatfs_vfs_list(&test_ctx, "/nodir", collect_cb, (void *)0)
          == VFS_ERR_NOTFOUND);
    CHECK(stub_closedir_calls == 0);
    return 0;
}

/* ケース 11: 空ディレクトリは 0 件で VFS_OK (エラーと区別する)。 */
static int case_list_empty_is_ok(void)
{
    reset();
    script(0, FR_OK, "", 0, 0);
    CHECK(fatfs_vfs_list(&test_ctx, "/etc", collect_cb, (void *)0) == VFS_OK);
    CHECK(seen_count == 0);
    CHECK(stub_closedir_calls == 1);
    return 0;
}

/* ======== 起動ログ (2026-09-25): 一括書き込みの最後のフラッシュ ======== */

/* ケース 12: f_write は通り f_close だけが落ちる (最後のセクタとディレクトリ
 * エントリの書き戻し)。直す前は f_close の結果を捨てて bw を返していたので、
 * 媒体に残っていないのに「書けた」になっていた。 */
static int case_write_close_fail_is_error(void)
{
    reset();
    stub_close_rc = FR_DISK_ERR;
    CHECK(fatfs_vfs_write(&test_ctx, "/var/log/boot.new", "abc", 3) == VFS_ERR_IO);
    CHECK(stub_write_calls == 1);
    CHECK(stub_close_calls == 1);

    /* 正常なら書いたバイト数 (bw)、f_close は 1 回 */
    reset();
    CHECK(fatfs_vfs_write(&test_ctx, "/var/log/boot.new", "abcd", 4) == 4);
    CHECK(stub_close_calls == 1);

    /* 満杯: f_write は FR_OK で bw < btw。そのまま bw を返す (呼び手が比べる) */
    reset();
    stub_write_bw = 2;
    CHECK(fatfs_vfs_write(&test_ctx, "/var/log/boot.new", "abcd", 4) == 2);
    CHECK(stub_close_calls == 1);
    return 0;
}

/* ケース 13: f_write の失敗が先。両方落ちても f_close は 1 回だけ (二重に
 * 閉じない)。f_open が落ちたら閉じない。 */
static int case_write_fail_wins_and_closes_once(void)
{
    reset();
    stub_write_rc = FR_DENIED;            /* → IO */
    stub_close_rc = FR_NO_FILE;           /* → NOTFOUND (来ないはず) */
    CHECK(fatfs_vfs_write(&test_ctx, "/var/log/boot.new", "abc", 3) == VFS_ERR_IO);
    CHECK(stub_close_calls == 1);

    reset();
    stub_open_rc = FR_NOT_READY;
    CHECK(fatfs_vfs_write(&test_ctx, "/var/log/boot.new", "abc", 3) == VFS_ERR_IO);
    CHECK(stub_write_calls == 0);
    CHECK(stub_close_calls == 0);
    return 0;
}

/* ---- 起動ログの手順を実物の fatfs_vfs_* に結ぶ (ctx は test_ctx) ---- */
static int bl_mkdir(const char *p) { return fatfs_vfs_mkdir(&test_ctx, p); }
static int bl_rm(const char *p) { return fatfs_vfs_unlink(&test_ctx, p); }
static int bl_rename(const char *a, const char *b) { return fatfs_vfs_rename(&test_ctx, a, b); }
static int bl_write(const char *p, const void *d, u32 n) { return fatfs_vfs_write(&test_ctx, p, d, n); }
static int bl_sync(void) { return fatfs_vfs_sync(&test_ctx); }
static const BootlogFsOps bl_ops = { bl_mkdir, bl_rm, bl_rename, bl_write, bl_sync };

/* ケース 14: 起動ログの保存で、実物の FAT の close が落ちる。手順は WRITE で
 * 止まり (rc は IO)、世代を動かさない (rename は 0 回)。unlink は書く前の
 * boot.new と、途中で切れた boot.new の後始末の 2 回 (どちらもその名前)。 */
static int case_bootlog_close_fail_keeps_logs(void)
{
    int st, rc = 0;

    reset();
    stub_close_rc = FR_DISK_ERR;
    st = bootlog_save_with(&bl_ops, BOOTLOG_FS_FAT, "LOG\n", 4, &rc);
    CHECK(st == BOOTLOG_ST_WRITE);
    CHECK(rc == VFS_ERR_IO);
    CHECK(stub_close_calls == 1);
    CHECK(stub_rename_calls == 0);
    CHECK(stub_unlink_calls == 2);
    CHECK(strcmp(stub_last_path, "0:/var/log/boot.new") == 0);

    /* 対照: close が通れば全段通る — unlink は書く前の boot.new だけ (贋物の
     * f_rename は宛先の有無を見ないので .1 は消さない)、rename は 2 回 */
    reset();
    st = bootlog_save_with(&bl_ops, BOOTLOG_FS_FAT, "LOG\n", 4, &rc);
    CHECK(st == BOOTLOG_ST_OK);
    CHECK(stub_close_calls == 1);
    CHECK(stub_unlink_calls == 1);
    CHECK(stub_rename_calls == 2);
    CHECK(strcmp(stub_last_path, "0:/var/log/boot.new") == 0);   /* 最後の rename の元 */
    return 0;
}

struct case_ent { const char *name; int (*fn)(void); };
static const struct case_ent cases[] = {
    { "stat_invalid_name_is_notfound",   case_stat_invalid_name_is_notfound },
    { "stat_missing_is_notfound",        case_stat_missing_is_notfound },
    { "stat_disk_err_is_io",             case_stat_disk_err_is_io },
    { "stat_ok_fills_size_mode",         case_stat_ok_fills_size_mode },
    { "get_size_invalid_name_is_notfound", case_get_size_invalid_name_is_notfound },
    { "open_paths_keep_inval",           case_open_paths_keep_inval },
    { "v50_journal_probe_on_8_3",        case_v50_journal_probe_on_8_3 },
    { "list_ok_enumerates_all",          case_list_ok_enumerates_all },
    { "list_readdir_error_propagates",   case_list_readdir_error_propagates },
    { "list_opendir_error_propagates",   case_list_opendir_error_propagates },
    { "list_empty_is_ok",                case_list_empty_is_ok },
    { "write_close_fail_is_error",       case_write_close_fail_is_error },
    { "write_fail_wins_and_closes_once", case_write_fail_wins_and_closes_once },
    { "bootlog_close_fail_keeps_logs",   case_bootlog_close_fail_keeps_logs }
};

int main(int argc, char **argv)
{
    unsigned i;
    if (argc < 2) {
        fprintf(stderr, "usage: %s <case>\n", argv[0]);
        return 2;
    }
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        if (strcmp(cases[i].name, argv[1]) == 0) return cases[i].fn();
    }
    fprintf(stderr, "unknown case: %s\n", argv[1]);
    return 2;
}
