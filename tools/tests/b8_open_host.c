/* ========================================================================
 *  b8_open_host.c — 読み取り失敗を「不存在」として扱う経路 (票 B8) を
 *  **実物の vfs_open() / vfs_open_sqlite() を通して**確かめる
 *
 *  票:   docs/archive/shell/TASK_FS_TYPE.md §2 (B8)
 *  実行: python3 -B tools/tests/test_b8_open.py
 *  記録: tools/tests/b8_tdd.md
 *
 *  なぜ open まで通すのか
 *  ---------------------
 *  vfs_path_kind() の戻り値までしか見ない試験では、同じ形の欠陥 (B7) を
 *  往復 4 で**実際に取り逃している**。判定を**消費する側**まで動かさないと、
 *  「読めなかった」が「無い」に化けて何が起きるかは見えない。
 *
 *  2 つの土台を 1 本の実行ファイルに載せる。
 *
 *   段 A: **実物の ext2** (fs/ext2_*.c) を RAM ディスク上で実物の ext2_format()
 *         が作った 8MB のファイルシステムに載せ、**実物の fs/vfs.c と
 *         fs/vfs_fd.c** から vfs_open() で叩く。贋物は Device API と IDE だけ。
 *         - §2-2a: **間接ブロックを使う大きなディレクトリ** (直接 12 本を
 *           越えた /big) の間接ブロック読み取りを**一度だけ**失敗させ、
 *           ext2_bmap -> find_entry -> lookup -> stat -> open の全段で
 *           「未割当 = 検索終了」に化けないことを見る。
 *         - §2-2b: 種別は確定しているのに**サイズ取得だけ**が一度失敗する
 *           組み合わせ (最も重い無言のデータ消失)。O_CREAT / O_TRUNC の
 *           4 通りすべてで、FD が出ず・媒体に 1 セクタも書かれず・
 *           既存の中身がそのまま残ることを確かめる。
 *
 *   段 B: 合成 VfsOps (tools/tests/vfs_kind_host.c と同じ作法)。stat と
 *         get_file_size の戻り値を 1 つずつ指定でき、**write_file の呼び出し
 *         回数をそのまま数えられる**ので、「作成へ進んでいない」ことを
 *         回数 0 で押さえる。vfs_open_sqlite も同じ土台で通す。
 *
 *  エミュレータ・実デバイス・実イメージ・make には一切触れない。
 *
 *  [C1] C89 / GNU89。宣言はブロック先頭、`//` コメント無し。
 *  u32 は `unsigned long` (include/types.h) なので**必ず ILP32 で組む**。
 *  libc は使わない (-nostdlib、Linux の int 0x80 で write/exit するだけ)。
 * ======================================================================== */

#include "ext2_priv.h"
#include "ide.h"
#include "kmalloc.h"
#include "hdd_pt_fake.h"   /* LBA 1 に OS32 区画 (票 TASK_HDD_INSTALL 段 1、1088 の廃止) */

/* ======================================================================== */
/*  libc の代わり (-nostdlib)                                               */
/* ======================================================================== */

static int g_exit_code;
static const char *g_dump_dir;   /* 票 B8 往復 5: e2fsck 用の像の置き場 (argv[1]) */

static void die(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code));
    for (;;) { }
}

static u32 h_strlen(const char *s) { u32 n = 0; while (s[n]) n++; return n; }

static void report(const char *text)
{
    u32 len = h_strlen(text);
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(text), "d"(len)
                     : "memory");
}

static void report_i(int v)
{
    char buf[16];
    int i = 15;
    u32 u;
    buf[i] = '\0';
    if (v < 0) u = (u32)(-v); else u = (u32)v;
    if (u == 0) { buf[--i] = '0'; }
    while (u > 0) { buf[--i] = (char)('0' + (u % 10)); u /= 10; }
    if (v < 0) buf[--i] = '-';
    report(&buf[i]);
}

static int g_checks;
static int g_failures;

static void check_at(int cond, const char *what, int line)
{
    g_checks++;
    if (cond) return;
    g_failures++;
    report("  FAIL line ");
    report_i(line);
    report(": ");
    report(what);
    report("\n");
}

#define CHECK(x) check_at((x) ? 1 : 0, #x, __LINE__)

/* ======================================================================== */
/*  kstring / kprintf / kmalloc の境界                                      */
/* ======================================================================== */

void *kmemcpy(void *dst, const void *src, u32 n)
{
    u8 *d = (u8 *)dst; const u8 *s = (const u8 *)src; u32 i = 0;
    /* 掃引 (段 C) は何千回も操作をやり直すので、4 バイトずつ写す */
    for (; i + 4 <= n; i += 4) *(u32 *)(d + i) = *(const u32 *)(s + i);
    for (; i < n; i++) d[i] = s[i];
    return dst;
}

void *kmemset(void *dst, int val, u32 n)
{
    u8 *d = (u8 *)dst; u32 i;
    for (i = 0; i < n; i++) d[i] = (u8)val;
    return dst;
}

u32 kstrlen(const char *s) { return h_strlen(s); }

int kstrcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (int)(u8)*a - (int)(u8)*b;
}

int kstrncmp(const char *a, const char *b, u32 n)
{
    u32 i;
    for (i = 0; i < n; i++) {
        if (a[i] != b[i]) return (int)(u8)a[i] - (int)(u8)b[i];
        if (!a[i]) return 0;
    }
    return 0;
}

char *kstrncpy(char *dst, const char *src, u32 n)
{
    u32 i = 0;
    if (n == 0) return dst;
    for (; i + 1 < n && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
    return dst;
}

char *kstrncat(char *dst, const char *src, u32 n)
{
    u32 d = h_strlen(dst);
    if (d + 1 >= n) return dst;
    kstrncpy(dst + d, src, n - d);
    return dst;
}

/* gcc が構造体コピー等で呼ぶことがある */
void *memcpy(void *dst, const void *src, u32 n) { return kmemcpy(dst, src, n); }
void *memset(void *dst, int val, u32 n) { return kmemset(dst, val, n); }

/* 票 B8 往復 5: マウント時の警告 / エラー状態の通知が出たかを数える */
static int g_kp_mount_warn;
static int g_kp_fs_error;
static int g_kp_rmdir_refused;     /* 往復 6: 空なのに links > 2 の rmdir を断った */
static int kp_has(const char *hay, const char *needle)
{
    u32 i, j;
    for (i = 0; hay[i]; i++) {
        for (j = 0; needle[j] && hay[i + j] == needle[j]; j++) { }
        if (!needle[j]) return 1;
    }
    return 0;
}
void kprintf(u8 attr, const char *fmt, ...)
{
    (void)attr;
    if (!fmt) return;
    if (kp_has(fmt, "mounting fs with errors")) g_kp_mount_warn++;
    if (kp_has(fmt, "writes disabled until remount")) g_kp_fs_error++;
    if (kp_has(fmt, "rmdir refused")) g_kp_rmdir_refused++;
}

/* ---- kzalloc / kfree: 固定スロットの贋物 (Ext2Ctx 専用) ---- */
#define HEAP_SLOTS  4
static struct { int in_use; Ext2Ctx ctx; } g_slots[HEAP_SLOTS];

void *kzalloc(u32 size)
{
    int i;
    if (size > sizeof(Ext2Ctx)) return (void *)0;
    for (i = 0; i < HEAP_SLOTS; i++) {
        if (!g_slots[i].in_use) {
            g_slots[i].in_use = 1;
            kmemset(&g_slots[i].ctx, 0, sizeof(Ext2Ctx));
            return (void *)&g_slots[i].ctx;
        }
    }
    return (void *)0;
}

void kfree(void *p)
{
    int i;
    for (i = 0; i < HEAP_SLOTS; i++) {
        if (p == (void *)&g_slots[i].ctx) { g_slots[i].in_use = 0; return; }
    }
}

/* ======================================================================== */
/*  RAM ディスク + **一度だけの I/O 失敗**の注入                            */
/* ======================================================================== */

/* 8MB の ext2。base_lba は LBA 1 の区画表 (hdd_pt_fake.h が標準配置で書く) の
 * 開始 = 1088 (区画はシリンダ単位 = FS をシリンダへ切り上げた長さ)。
 *
 * 票 B8 往復 6: 8MB (8192 ブロック) は **1 グループ**に収まるので、グループを
 * またぐ割り当て (ext2_alloc_block が次のグループへ進む) が試験から見えなかった。
 * 実 NHD は 25 グループ。ext2_format はグループの大きさを選べない
 * (EXT2_BLOCKS_PER_GROUP_MAX = 8192 固定) ので、**ディスクを大きくして**
 * 複数グループを作る。既定の 8MB はそのまま (既存の掃引の回数・配置を変えない)、
 * 複数グループの case だけが g_fs_sectors を DISK_GROUPS_FS_SECTORS にする。 */
#define DISK_FS_SECTORS        16384u
#define DISK_GROUPS_FS_SECTORS 36000u   /* 18000 ブロック = 3 グループ (8192 / 8192 / 1615) */
#define DISK_BASE_LBA          1088u
#define DISK_MAX_SECTORS       (DISK_BASE_LBA + PT_FAKE_ROUNDUP(DISK_GROUPS_FS_SECTORS))
static u32 g_fs_sectors = DISK_FS_SECTORS;
#define DISK_SECTORS           (DISK_BASE_LBA + PT_FAKE_ROUNDUP(g_fs_sectors))

static u8 g_disk[DISK_MAX_SECTORS * 512u];
static u32 g_rd_sect;
static u32 g_wr_sect;
/* 票 B8 往復 5: スーパーブロックの先頭セクタ (s_state が居る) への書き込み。
 * メタデータの I/O が落ちると ext2_fs_error がここへ EXT2_ERROR_FS を書くので、
 * 「失敗した操作は何も書かない」を見る試験はこれを除いて数える。 */
static u32 g_wr_sb;
#define SB_STATE_LBA  (DISK_BASE_LBA + 2u)

/* 失敗の注入: 「この LBA の n 回目の読み出しを**一度だけ**失敗させる」。
 * 一度だけにするのが肝 — 恒久的な故障なら誰が見ても異常だが、B8 が起きるのは
 * **一度読めなかっただけで次は読める**ときである。 */
static u32 g_fail_lba;
static int g_fail_armed;      /* 1 = 仕掛けてある */
static int g_fail_nth;        /* 何回目の一致を落とすか (1 起算) */
static int g_fail_seen;       /* 仕掛けてからの一致回数 */
static int g_fail_fired;      /* 実際に落とした回数 */

static void fail_arm(u32 lba, int nth)
{
    g_fail_lba = lba; g_fail_armed = 1; g_fail_nth = nth;
    g_fail_seen = 0; g_fail_fired = 0;
}

static void fail_arm_always(u32 lba) { fail_arm(lba, 0); }

static void fail_disarm(void) { g_fail_armed = 0; }

static void io_reset(void) { g_rd_sect = 0; g_wr_sect = 0; g_wr_sb = 0; }
/* s_state のセクタ以外への書き込み回数 */
static u32 wr_non_state(void) { return g_wr_sect - g_wr_sb; }

/* ---- 往復 3: **書き込み**の失敗注入 (読み出し側と同じ作法) ---- */
static u32 g_wfail_lba;
static int g_wfail_armed;
static int g_wfail_nth;       /* 0 = armed のあいだずっと */
static int g_wfail_seen;
static int g_wfail_fired;
/* 票 H2 X3b (ii) / 往復 3 所見 2: **媒体へ写した後で失敗を返す**模様。
 * drivers/ide.c は状態レジスタをデータ転送の**後**に見るので、
 * 「書き込みが失敗した = 媒体は未変更」は成り立たない。従来の注入は写す前に
 * 返していたので、この形は 1 度も作れていなかった。 */
static int g_wfail_land;
static int g_sw_land;         /* 掃引側の同じ模様 */
/* 票 H2 X3b (iii): 書き込みが落ちた**後の読み直しも落とす**。
 * 公開の 3 値のうち「不明」を作れる唯一の形で、ここでは D を解放しては
 * いけない (生きている内容の解放は戻せない)。 */
static int g_wfail_then_rfail;

static void wfail_arm(u32 lba, int nth)
{
    g_wfail_lba = lba; g_wfail_armed = 1; g_wfail_nth = nth;
    g_wfail_seen = 0; g_wfail_fired = 0; g_wfail_land = 0;
    g_wfail_then_rfail = 0;
}

/* 媒体へ写してから失敗を返す版 */
static void wfail_arm_landed(u32 lba, int nth)
{
    wfail_arm(lba, nth);
    g_wfail_land = 1;
}
static void wfail_arm_always(u32 lba) { wfail_arm(lba, 0); }
static void wfail_disarm(void) { g_wfail_armed = 0; }

/* ---- 往復 3: 掃引用の注入 ----
 * 仕掛けてから at 回目の**セクタ I/O** (読み書き問わず、kind で絞れる) を落とす。
 * sticky = 1 なら at 回目**以降すべて**を落とす (装置が途中で消えた)。
 * 1KB ブロック = 2 セクタなので、書き込みの 2 セクタ目だけが落ちる
 * (= ブロックの前半だけ新しくなる) 場合も自然に作られる。 */
#define SW_KIND_ANY    0
#define SW_KIND_READ   1
#define SW_KIND_WRITE  2
static int g_sw_armed, g_sw_at, g_sw_sticky, g_sw_kind, g_sw_seen, g_sw_fired;
/* 段 H (二重故障): 2 つ目の位置。0 なら従来どおり 1 か所だけ。 */
static int g_sw_at2, g_sw_fired1, g_sw_fired2;

static void sw_arm(int at, int sticky, int kind)
{
    g_sw_armed = 1; g_sw_at = at; g_sw_sticky = sticky; g_sw_kind = kind;
    g_sw_seen = 0; g_sw_fired = 0;
    g_sw_at2 = 0; g_sw_fired1 = 0; g_sw_fired2 = 0;
    g_sw_land = 0;
}

/* 「i 番目と j 番目の I/O だけが落ちる」(i < j)。sticky は使わない。
 * 位置は**実際に走った I/O の順番**なので、1 つ目で経路が変わった後の j も
 * その走行の中での j 番目になる。 */
static void sw_arm_pair(int at, int at2)
{
    sw_arm(at, 0, SW_KIND_ANY);
    g_sw_at2 = at2;
}
static void sw_disarm(void) { g_sw_armed = 0; }

static int sw_hit(int kind)
{
    if (!g_sw_armed) return 0;
    if (g_sw_kind != SW_KIND_ANY && g_sw_kind != kind) return 0;
    g_sw_seen++;
    if (g_sw_seen == g_sw_at || (g_sw_sticky && g_sw_seen > g_sw_at)) {
        g_sw_fired++; g_sw_fired1++;
        return 1;
    }
    if (g_sw_at2 && g_sw_seen == g_sw_at2) {
        g_sw_fired++; g_sw_fired2++;
        return 1;
    }
    return 0;
}

/* ---- 往復 3: 書き込みの取り消し記録 (掃引の 1 回ごとにディスクを戻す) ---- */
#define UNDO_MAX  8192
static u32 g_undo_lba[UNDO_MAX];
static u8  g_undo_data[UNDO_MAX * 512u];
static u8  g_undo_mark[(DISK_MAX_SECTORS + 7) / 8];
static int g_undo_n, g_undo_on, g_undo_overflow;

/* ---- 往復 3: 書き込み先の記録 (空打ちで配置を知る) ---- */
#define WLOG_MAX  8192
static u32 g_wlog_lba[WLOG_MAX];
static int g_wlog_n, g_wlog_on;

static void undo_record(u32 lba)
{
    if (!g_undo_on) return;
    if (g_undo_mark[lba / 8] & (1 << (lba % 8))) return;
    if (g_undo_n >= UNDO_MAX) { g_undo_overflow = 1; return; }
    g_undo_mark[lba / 8] |= (u8)(1 << (lba % 8));
    g_undo_lba[g_undo_n] = lba;
    kmemcpy(g_undo_data + (u32)g_undo_n * 512u, g_disk + lba * 512u, 512);
    g_undo_n++;
}

static void undo_begin(void) { g_undo_n = 0; g_undo_on = 1; }

static void undo_rollback(void)
{
    int i;
    for (i = g_undo_n - 1; i >= 0; i--) {
        u32 lba = g_undo_lba[i];
        kmemcpy(g_disk + lba * 512u, g_undo_data + (u32)i * 512u, 512);
        g_undo_mark[lba / 8] &= (u8)~(1 << (lba % 8));
    }
    g_undo_n = 0;
    g_undo_on = 0;
}

static Device g_hd0;

Device *dev_find(const char *name)
{
    if (name && name[0] == 'h' && name[1] == 'd' && name[2] == '0' && !name[3])
        return &g_hd0;
    return (Device *)0;
}

int dev_blk_read_lba(Device *dev, u32 lba, int count, void *buf)
{
    int i;
    if (!dev) return -1;
    for (i = 0; i < count; i++) {
        u32 cur = lba + (u32)i;
        if (cur >= DISK_SECTORS) return -1;
        if (sw_hit(SW_KIND_READ)) return -1;
        if (g_fail_armed && cur == g_fail_lba) {
            g_fail_seen++;
            /* nth == 0 は「armed のあいだずっと落とす」(本当の不良セクタ) */
            if (g_fail_nth == 0 || g_fail_seen == g_fail_nth) {
                g_fail_fired++;
                return -1;
            }
        }
        kmemcpy((u8 *)buf + i * 512, g_disk + cur * 512u, 512);
        g_rd_sect++;
    }
    return 0;
}

int dev_blk_write_lba(Device *dev, u32 lba, int count, const void *buf)
{
    int i;
    if (!dev) return -1;
    for (i = 0; i < count; i++) {
        u32 cur = lba + (u32)i;
        int land = 0;
        if (cur >= DISK_SECTORS) return -1;
        if (sw_hit(SW_KIND_WRITE)) {
            if (!g_sw_land) return -1;
            land = 1;                         /* 届いてから失敗する */
        }
        if (!land && g_wfail_armed && cur == g_wfail_lba) {
            g_wfail_seen++;
            if (g_wfail_nth == 0 || g_wfail_seen == g_wfail_nth) {
                g_wfail_fired++;
                if (g_wfail_then_rfail) {
                    g_fail_lba = cur; g_fail_armed = 1; g_fail_nth = 0;
                    g_fail_seen = 0;
                }
                if (!g_wfail_land) return -1;
                land = 1;
            }
        }
        undo_record(cur);
        if (g_wlog_on && g_wlog_n < WLOG_MAX) g_wlog_lba[g_wlog_n++] = cur;
        kmemcpy(g_disk + cur * 512u, (const u8 *)buf + i * 512, 512);
        g_wr_sect++;
        if (cur == SB_STATE_LBA) g_wr_sb++;
        if (land) return -1;                  /* 媒体には届いたが失敗を返す */
    }
    return 0;
}

/* ---- IDE の境界 (ext2_super.c / ext2_fmt.c が存在確認とジオメトリに使う) */
int ide_drive_present(int drive) { return (drive & 3) == 0; }

int ide_get_info(int drive, IdeInfo *info)
{
    if ((drive & 3) != 0) return IDE_ERR_NO_DRIVE;
    if (info) {
        kmemset(info, 0, sizeof(*info));
        info->cylinders = 1024;
        info->heads = 8;
        info->sectors = 17;
        info->phys_sector_size = 512;
        info->total_sectors = DISK_SECTORS;
    }
    return IDE_OK;
}

/* ======================================================================== */
/*  実物のソース                                                            */
/* ======================================================================== */

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

/* HostDrv 側のサイズ取得の純規則も実物を取り込む (③、fs/hostdrvfs.c が使う)。
 * hostdrvfs.c 本体はハイパーコールを叩くのでホストでは組めないが、
 * 判定はこの .inc の純関数に切り出してある (票 H1 と同じ作法)。 */
#include "../../fs/hostdrv_stat_rules.inc"

#include "../../fs/ext2_super.c"
#include "../../fs/ext2_inode.c"
#include "../../fs/ext2_dir.c"
#include "../../fs/ext2_file.c"
#include "../../fs/ext2_fmt.c"
#include "../../fs/ext2_vfs.c"
#include "../../fs/vfs.c"
#include "../../fs/vfs_fd.c"

/* ======================================================================== */
/*  足場                                                                    */
/* ======================================================================== */

static Ext2Ctx *g_ec;
static void remount_cold(void);

/* 票 B8 往復 5: 失敗注入の後始末。メタデータの I/O が落ちるとマウントが
 * エラー状態になり、以後の書き込み系操作を断る (ユーザー決裁 2)。同じディスクで
 * 続きを試すには**再マウントする** — 電源を入れ直したのと同じで sync はしない
 * (エラー状態はメモリ上だけなので、再マウントで解ける)。 */
static void fault_done(void) { if (g_ec) remount_cold(); }

/* /big/keepme の中身。**open が失敗したあともここがそのまま残ること**が、
 * 段 A の一番大事な確認。 */
static const char KEEP_TEXT[] = "KEEPME-0123456789-do-not-erase";
#define KEEP_LEN  30                  /* NUL を含めない */

/* 直接ブロック 12 本 (12KB) を越えさせるための詰め物。
 * 名前 6 文字 -> rec_len = (8+6+3)&~3 = 16 バイト -> 1 ブロック 64 件。
 * 12 ブロックで 768 件なので、それを十分に越える数を入れる。 */
#define FILLER_COUNT  900

static void fd_table_reset(void)
{
    int i;
    for (i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        open_files[i].in_use = 0;
        open_files[i].generation = 0;
    }
}

static void vfs_tables_reset(void)
{
    int i;
    num_fs = 0;
    for (i = 0; i < VFS_MAX_FS; i++) kmemset(&mounts[i], 0, sizeof(mounts[i]));
    fd_table_reset();
    kstrncpy(cwd, "/", VFS_MAX_PATH);
}

static void filler_name(char *dst, int n)
{
    dst[0] = 'f';
    dst[1] = (char)('0' + (n / 1000) % 10);
    dst[2] = (char)('0' + (n / 100) % 10);
    dst[3] = (char)('0' + (n / 10) % 10);
    dst[4] = (char)('0' + n % 10);
    dst[5] = 'x';
    dst[6] = '\0';
}

/* /big/keepme の中身を読み直す。**open を通さず** ext2 の read_file で直に
 * 見るので、「open が壊したかどうか」を open 自身に聞かずに済む。 */
static int keep_intact(void)
{
    static u8 buf[128];
    int n;
    u32 ino;
    kmemset(buf, 0, sizeof(buf));
    if (ext2_lookup(g_ec, "/big/keepme", &ino) != EXT2_OK) return 0;
    n = ext2_read_file(g_ec, ino, buf, sizeof(buf));
    if (n != KEEP_LEN) return 0;
    return kstrncmp((const char *)buf, KEEP_TEXT, KEEP_LEN) == 0;
}

static u32 keep_ino(void)
{
    u32 ino = 0;
    ext2_lookup(g_ec, "/big/keepme", &ino);
    return ino;
}

/* inode 番号 -> その inode が載っている **1KB ブロックの先頭セクタ LBA**。
 * fs/ext2_inode.c の ext2_read_inode と同じ式。 */
static u32 lba_of_inode(u32 ino)
{
    u32 group = (ino - 1) / g_ec->sb_info.inodes_per_group;
    u32 index = (ino - 1) % g_ec->sb_info.inodes_per_group;
    u32 blk = g_ec->gd_table[group].inode_table
              + (index * g_ec->sb_info.inode_size) / EXT2_BLOCK_SIZE;
    return g_ec->base_lba + blk * 2;
}

/* ディレクトリ /big の**間接ブロック**の先頭セクタ LBA (§2-2a の経路) */
static u32 lba_of_big_indirect(void)
{
    Ext2Inode dir;
    u32 ino = 0;
    CHECK(ext2_lookup(g_ec, "/big", &ino) == EXT2_OK);
    CHECK(ext2_read_inode(g_ec, ino, &dir) == EXT2_OK);
    /* ここが 0 なら詰め物が足りず、そもそも間接ブロックを使っていない */
    CHECK(dir.block[EXT2_IND_BLOCK] != 0);
    return g_ec->base_lba + dir.block[EXT2_IND_BLOCK] * 2;
}

static void disk_setup(void)
{
    int i;
    char name[8];

    kmemset(g_disk, 0, sizeof(g_disk));
    CHECK(pt_fake_write(g_disk, DISK_BASE_LBA, DISK_SECTORS - DISK_BASE_LBA) == 0);
    kmemset(&g_hd0, 0, sizeof(g_hd0));
    g_hd0.name = "hd0";
    g_hd0.type = DEV_BLOCK;
    g_hd0.bus_type = DEV_BUS_IDE;
    g_hd0.sect_size = 512;
    g_hd0.total_sects = DISK_SECTORS;
    g_hd0.heads = 8;
    g_hd0.spt = 17;
    fail_disarm();

    CHECK(ext2_format(0, g_fs_sectors) == EXT2_OK);

    vfs_tables_reset();
    ext2_init();                               /* 実物の登録 */
    CHECK(vfs_mount("/", "hd0", "ext2") == VFS_OK);
    g_ec = (Ext2Ctx *)mounts[0].fs_ctx;
    CHECK(g_ec != (Ext2Ctx *)0);
    CHECK(g_ec->base_lba == DISK_BASE_LBA);

    /* 間接ブロックを使う大きなディレクトリ (§2-2a)。
     * 詰め物の 900 件は足場なので CHECK で数えず、失敗したらその場で止める。 */
    CHECK(ext2_vfs_mkdir(g_ec, "/big") == VFS_OK);
    {
        u32 dir_ino = 0;
        CHECK(ext2_lookup(g_ec, "/big", &dir_ino) == EXT2_OK);
        for (i = 0; i < FILLER_COUNT; i++) {
            filler_name(name, i);
            if (ext2_create(g_ec, dir_ino, name, "", 0) != EXT2_OK) {
                report("  (harness) filler create failed at ");
                report_i(i); report("\n");
                g_failures++;
                return;
            }
        }
    }
    /* 目当てのファイルは**最後**に入れる = 間接ブロック側のブロックに載る */
    CHECK(ext2_vfs_write(g_ec, "/big/keepme", KEEP_TEXT, KEEP_LEN) == VFS_OK);
    CHECK(keep_intact());

    /* 通常ファイルとディレクトリの回帰用 */
    CHECK(ext2_vfs_mkdir(g_ec, "/etc") == VFS_OK);
    CHECK(ext2_vfs_write(g_ec, "/etc/plain", "PLAIN", 5) == VFS_OK);
}

static void disk_teardown(void)
{
    fail_disarm();
    wfail_disarm();
    sw_disarm();
    if (g_ec) { vfs_umount("/"); g_ec = (Ext2Ctx *)0; }
}

/* 記憶 (票 S6-P の解決済み経路) を捨てて、必ず実際に辿り直させる */
static void memo_cold(void) { ext2_path_memo_reset(g_ec); }

static int any_fd_open(void)
{
    int i, n = 0;
    for (i = 0; i < VFS_MAX_OPEN_FILES; i++) if (open_files[i].in_use) n++;
    return n;
}

/* ======================================================================== */
/*  段 A: 実物の ext2 + 実物の vfs_open                                     */
/* ======================================================================== */

/* §2-2a — 間接ブロックが一度読めなかっただけで「無い」に化けないこと。
 * 直す前: ext2_bmap が読み取り失敗を 0 (= 未割当) に潰す
 *         -> ext2_find_entry が「検索終了」と読んで NOTFOUND
 *         -> ext2_lookup / ext2_vfs_stat も NOTFOUND
 *         -> vfs_path_kind が NOTFOUND -> open が O_CREAT 経路へ進む。 */
static void case_indirect_read_failure(int mode, const char *label)
{
    int fd;
    u32 lba;

    report("  [A1] "); report(label); report("\n");
    fault_done();   /* 前の case のエラー状態を持ち越さない (票 B8 往復 5) */

    memo_cold();
    lba = lba_of_big_indirect();
    io_reset();
    fail_arm(lba, 1);
    fd = vfs_open("/big/keepme", mode);
    fail_disarm();
    fault_done();

    CHECK(g_fail_fired == 1);            /* 仕掛けが本当に効いたか */
    CHECK(fd < 0);                        /* FD を発行しない */
    CHECK(fd != VFS_ERR_NOTFOUND);        /* 「無い」と言わない */
    CHECK(any_fd_open() == 0);
    CHECK(wr_non_state() == 0);                /* 媒体に 1 セクタも書いていない */
    CHECK(keep_intact());                 /* 中身がそのまま残っている */
}

/* §2-2b — **最も重い経路**。種別は stat で「通常ファイル」と確定しているのに、
 * 直後のサイズ取得だけが一度 I/O に失敗する。
 * 直す前: vfs_open_internal が「サイズが取れない = 存在しない」と読み、
 *         O_CREAT が付いていれば **O_TRUNC が無くても** 0 バイトで上書き。
 *
 * 1 回の vfs_open のあいだ、目当ての inode が載るブロックは
 *   1 回目 = vfs_path_kind -> ext2_vfs_stat の ext2_read_inode
 *   2 回目 = ops->get_file_size -> ext2_get_size_ino の ext2_read_inode
 * の 2 回読まれる (記憶が温まっていればパス解決は辿り直さない)。
 * その **2 回目だけ**を落とす。 */
static void case_size_failure(int mode, const char *label)
{
    int fd;
    u32 lba;

    report("  [A2] "); report(label); report("\n");
    fault_done();   /* 前の case のエラー状態を持ち越さない (票 B8 往復 5) */

    /* 記憶を温めて、パス解決が inode 表を読み直さない状態にする */
    memo_cold();
    CHECK(vfs_path_kind("/big/keepme") == VFS_KIND_FILE);

    lba = lba_of_inode(keep_ino());
    io_reset();
    fail_arm(lba, 2);
    fd = vfs_open("/big/keepme", mode);
    fail_disarm();
    fault_done();

    CHECK(g_fail_fired == 1);
    CHECK(fd < 0);
    CHECK(fd != VFS_ERR_NOTFOUND);
    CHECK(any_fd_open() == 0);
    CHECK(wr_non_state() == 0);
    CHECK(keep_intact());
}

static void case_sqlite_size_failure(void)
{
    VfsSqliteCookie ck;
    VfsSqliteLease lease;
    int rc;
    u32 lba;

    report("  [A3] vfs_open_sqlite も同じ (サイズ取得の一度の失敗)\n");
    fault_done();   /* 前の case のエラー状態を持ち越さない (票 B8 往復 5) */

    ck.group_index = 0;
    ck.generation = 1;
    kmemset(&lease, 0, sizeof(lease));

    memo_cold();
    CHECK(vfs_path_kind("/big/keepme") == VFS_KIND_FILE);

    lba = lba_of_inode(keep_ino());
    io_reset();
    fail_arm(lba, 2);
    rc = vfs_open_sqlite("/big/keepme", O_RDWR | O_CREAT, 0, &ck, 0, &lease);
    fail_disarm();
    fault_done();

    CHECK(g_fail_fired == 1);
    CHECK(rc < 0);
    CHECK(rc != VFS_ERR_NOTFOUND);
    CHECK(any_fd_open() == 0);
    CHECK(wr_non_state() == 0);
    CHECK(keep_intact());
}

/* /big の中の "keepme" という名前のエントリ数。一括書き込みが「無い」と
 * 誤判断して ext2_create へ落ちると、同じ名前が 2 つ並ぶ。 */
static int g_name_hits;
static void count_cb(const Ext2DirEntry *e, void *ctx)
{
    (void)ctx;
    if (e->name_len == 6 && kstrncmp(e->name, "keepme", 6) == 0) g_name_hits++;
}

static int keep_entry_count(void)
{
    u32 ino = 0;
    g_name_hits = 0;
    if (ext2_lookup(g_ec, "/big", &ino) != EXT2_OK) return -1;
    if (ext2_list_dir(g_ec, ino, count_cb, (void *)0) != EXT2_OK) return -1;
    return g_name_hits;
}

/* ext2_vfs_write (一括書き込み) — 既存かどうかの判定が読めなかったときに
 * **新規作成へ落ちない**こと。落ちると同じ名前の二重エントリができ、
 * 元の inode が名前から辿れなくなる。 */
static void case_write_file_failure(void)
{
    int rc;
    u32 lba;

    report("  [A6] 一括書き込み: 既存判定が読めなければ作成しない\n");
    fault_done();   /* 前の case のエラー状態を持ち越さない (票 B8 往復 5) */

    /* 親 "/big" だけ記憶を温める — パス解決は通り、find_entry が失敗する */
    memo_cold();
    {
        OS32_Stat st;
        CHECK(ext2_vfs_stat(g_ec, "/big", &st) == VFS_OK);
    }

    CHECK(keep_entry_count() == 1);
    lba = lba_of_big_indirect();
    io_reset();
    fail_arm(lba, 1);
    rc = ext2_vfs_write(g_ec, "/big/keepme", "XX", 2);
    fail_disarm();
    fault_done();

    CHECK(g_fail_fired == 1);
    /* **読めなかったことを読めなかったと言う**。ここを「無い」と読み替えると
     * ext2_create へ落ち、その中の存在確認が (一度きりの失敗なら) 今度は通って
     * EXIST になる — 呼び手には「既にある」と見え、I/O 障害が隠れる。
     * 恒久的な失敗なら二重エントリになり、元の inode が名前から辿れなくなる。 */
    CHECK(rc == VFS_ERR_IO);
    CHECK(keep_entry_count() == 1);      /* 二重エントリを作っていない */
    CHECK(keep_intact());                /* 中身もそのまま */

    /* 本当の不良セクタ (ずっと読めない) でも作成へ落ちない */
    memo_cold();
    {
        OS32_Stat st;
        CHECK(ext2_vfs_stat(g_ec, "/big", &st) == VFS_OK);
    }
    lba = lba_of_big_indirect();
    fail_arm_always(lba);
    rc = ext2_vfs_write(g_ec, "/big/keepme", "YY", 2);
    fail_disarm();
    fault_done();
    CHECK(g_fail_fired > 0);
    CHECK(rc == VFS_ERR_IO);
    CHECK(keep_entry_count() == 1);
    CHECK(keep_intact());
}

/* ext2_write_stream — 間接ブロックが読めなかったのを「未割当」と見なして
 * **新しいブロックを割り当て直すと、元の中身を捨てる**。 */
static void case_write_stream_failure(void)
{
    static u8 pattern[16 * 1024];
    static u8 got[2 * 1024];
    Ext2Inode fi;
    u32 ino = 0, lba, i;
    int rc;

    report("  [A7] 追記書き込み: 読めない間接ブロックを割り当て直さない\n");
    fault_done();   /* 前の case のエラー状態を持ち越さない (票 B8 往復 5) */

    for (i = 0; i < sizeof(pattern); i++) pattern[i] = (u8)(i * 7 + 1);
    /* 16KB = 16 ブロック -> 直接 12 本を越えて間接ブロックを使う */
    CHECK(ext2_vfs_write(g_ec, "/big/wide", pattern, sizeof(pattern)) == VFS_OK);

    memo_cold();
    CHECK(ext2_lookup(g_ec, "/big/wide", &ino) == EXT2_OK);
    CHECK(ext2_read_inode(g_ec, ino, &fi) == EXT2_OK);
    CHECK(fi.block[EXT2_IND_BLOCK] != 0);
    lba = g_ec->base_lba + fi.block[EXT2_IND_BLOCK] * 2;

    /* 記憶を温めてから、間接ブロックの読み出しだけを一度落とす */
    {
        OS32_Stat st;
        CHECK(ext2_vfs_stat(g_ec, "/big/wide", &st) == VFS_OK);
    }
    io_reset();
    fail_arm(lba, 1);
    rc = ext2_vfs_write_stream(g_ec, "/big/wide", "ZZZZ", 4, 13 * 1024 + 100);
    fail_disarm();
    fault_done();

    CHECK(g_fail_fired == 1);
    CHECK(rc < 0);                       /* 「0 バイト書けた」で済ませない */

    /* 論理ブロック 13 の中身が丸ごと残っていること。割り当て直していたら
     * ここはゼロ埋めの新しいブロックに化けている。 */
    kmemset(got, 0, sizeof(got));
    CHECK(ext2_read_stream(g_ec, ino, got, 1024, 13 * 1024) == 1024);
    CHECK(kstrncmp((const char *)got, (const char *)&pattern[13 * 1024], 64) == 0);
    {
        int same = 1;
        for (i = 0; i < 1024; i++) {
            if (got[i] != pattern[13 * 1024 + i]) { same = 0; break; }
        }
        CHECK(same);
    }

    /* 正常時は同じ書き込みがちゃんと通る (回帰) */
    CHECK(ext2_vfs_write_stream(g_ec, "/big/wide", "ZZZZ", 4,
                                13 * 1024 + 100) == 4);
    kmemset(got, 0, sizeof(got));
    CHECK(ext2_read_stream(g_ec, ino, got, 1024, 13 * 1024) == 1024);
    CHECK(kstrncmp((const char *)&got[100], "ZZZZ", 4) == 0);
    CHECK(got[0] == pattern[13 * 1024]);
}

/* ディレクトリ ino の**直接ブロック 0** の先頭セクタ LBA */
static u32 lba_of_block0(u32 ino)
{
    Ext2Inode n;
    CHECK(ext2_read_inode(g_ec, ino, &n) == EXT2_OK);
    CHECK(n.block[0] != 0);
    return g_ec->base_lba + n.block[0] * 2;
}

static int g_entry_total;
static void total_cb(const Ext2DirEntry *e, void *ctx)
{ (void)e; (void)ctx; g_entry_total++; }

/* VFS 層の一覧用 (NULL コールバックは ext2_to_vfs_cb が呼び出して落ちる) */
static int g_vfs_entries;
static void vfs_total_cb(const VfsDirEntry *e, void *ctx)
{ (void)e; (void)ctx; g_vfs_entries++; }

static int dir_entry_total(const char *path)
{
    u32 ino = 0;
    g_entry_total = 0;
    if (ext2_lookup(g_ec, path, &ino) != EXT2_OK) return -1;
    if (ext2_list_dir(g_ec, ino, total_cb, (void *)0) != EXT2_OK) return -1;
    return g_entry_total;
}

/* ext2_bmap の**残りの呼び手**が、新しい区別を正しく扱っているか。
 * 1 つでも「読めなかった」を「未割当 = ここで終わり」と読む呼び手が残ると
 * 修正の意味が無い。 */
static void case_other_bmap_callers(void)
{
    u32 big_ino = 0, ind_lba, blk0_lba;
    int n, rc;

    report("  [A8] ext2_bmap の残りの呼び手\n");
    fault_done();   /* 前の case のエラー状態を持ち越さない (票 B8 往復 5) */

    memo_cold();
    CHECK(ext2_lookup(g_ec, "/big", &big_ino) == EXT2_OK);
    ind_lba = lba_of_big_indirect();

    /* (1) ext2_list_dir — **打ち切った一覧を成功として返さない** */
    n = dir_entry_total("/big");
    CHECK(n > FILLER_COUNT);                  /* 全件見えている */
    fail_arm(ind_lba, 1);
    g_entry_total = 0;
    rc = ext2_list_dir(g_ec, big_ino, total_cb, (void *)0);
    fail_disarm();
    fault_done();
    CHECK(g_fail_fired == 1);
    CHECK(rc == EXT2_ERR_IO);                 /* 途中までを成功と言わない */

    /* VFS 層 (ext2_vfs_list) でも同じ */
    memo_cold();
    {
        OS32_Stat st;
        CHECK(ext2_vfs_stat(g_ec, "/big", &st) == VFS_OK);
    }
    fail_arm(ind_lba, 1);
    g_vfs_entries = 0;
    rc = ext2_vfs_list(g_ec, "/big", vfs_total_cb, (void *)0);
    fail_disarm();
    fault_done();
    CHECK(g_fail_fired == 1);
    CHECK(rc == VFS_ERR_IO);

    /* (2) ext2_add_entry — 読めないまま抜けて新ブロックを継ぎ足さない。
     * 継ぎ足すと bmap_set(12) が**既存の間接ブロックの割り当てを上書き**し、
     * 間接側のエントリが丸ごと行方不明になる。 */
    n = dir_entry_total("/big");
    fail_arm(ind_lba, 1);
    rc = ext2_add_entry(g_ec, big_ino, "zzz", 2, EXT2_FT_REG_FILE);
    fail_disarm();
    fault_done();
    CHECK(g_fail_fired == 1);
    CHECK(rc == EXT2_ERR_IO);
    CHECK(dir_entry_total("/big") == n);      /* 1 件も失っていない */
    CHECK(keep_intact());

    /* (3) ext2_delete_entry */
    fail_arm(ind_lba, 1);
    rc = ext2_delete_entry(g_ec, big_ino, "keepme");
    fail_disarm();
    fault_done();
    CHECK(g_fail_fired == 1);
    CHECK(rc == EXT2_ERR_IO);                 /* NOTFOUND と言わない */
    CHECK(keep_intact());

    /* (4) ext2_read_file / ext2_read_stream — **短いファイル**に化けない */
    {
        static u8 got[20 * 1024];
        u32 wide_ino = 0;
        u32 wide_ind;
        Ext2Inode wi;

        memo_cold();
        CHECK(ext2_lookup(g_ec, "/big/wide", &wide_ino) == EXT2_OK);
        CHECK(ext2_read_inode(g_ec, wide_ino, &wi) == EXT2_OK);
        wide_ind = g_ec->base_lba + wi.block[EXT2_IND_BLOCK] * 2;

        CHECK(ext2_read_file(g_ec, wide_ino, got, sizeof(got)) == 16 * 1024);

        fail_arm(wide_ind, 1);
        rc = ext2_read_file(g_ec, wide_ino, got, sizeof(got));
        fail_disarm();
        fault_done();
        CHECK(g_fail_fired == 1);
        CHECK(rc == EXT2_ERR_IO);             /* 12KB の「成功」にしない */

        fail_arm(wide_ind, 1);
        rc = ext2_read_stream(g_ec, wide_ino, got, 16 * 1024, 0);
        fail_disarm();
        fault_done();
        CHECK(g_fail_fired == 1);
        CHECK(rc == EXT2_ERR_IO);
    }

    /* (5) ext2_is_dir_empty (rmdir 経由) — 読めなかったのを NOTEMPTY と偽らない */
    {
        u32 parent = 0, victim = 0;
        CHECK(ext2_vfs_mkdir(g_ec, "/rmtest") == VFS_OK);
        memo_cold();
        CHECK(ext2_lookup(g_ec, "/", &parent) == EXT2_OK);
        CHECK(ext2_lookup(g_ec, "/rmtest", &victim) == EXT2_OK);
        blk0_lba = lba_of_block0(victim);

        fail_arm(blk0_lba, 1);
        rc = ext2_rmdir(g_ec, parent, "rmtest");
        fail_disarm();
        fault_done();
        CHECK(g_fail_fired == 1);
        CHECK(rc == EXT2_ERR_IO);
        CHECK(rc != EXT2_ERR_NOTEMPTY);

        /* 読めるなら従来どおり消せる (回帰) */
        CHECK(ext2_rmdir(g_ec, parent, "rmtest") == EXT2_OK);
    }

    /* (6) ext2_parent_of (rename の循環検査) — 読めなかったのを
     * 「祖先ではない」と言うと、ディレクトリを自分の配下へ移せてしまう。 */
    {
        u32 root = 0, d1 = 0, d2 = 0;
        CHECK(ext2_vfs_mkdir(g_ec, "/d1") == VFS_OK);
        CHECK(ext2_vfs_mkdir(g_ec, "/d1/d2") == VFS_OK);
        memo_cold();
        CHECK(ext2_lookup(g_ec, "/", &root) == EXT2_OK);
        CHECK(ext2_lookup(g_ec, "/d1", &d1) == EXT2_OK);
        CHECK(ext2_lookup(g_ec, "/d1/d2", &d2) == EXT2_OK);

        /* 読めれば従来どおり断る */
        CHECK(ext2_rename(g_ec, root, "d1", d2, "moved") == EXT2_ERR_INVAL);

        /* ".." が読めないときは **INVAL とも OK とも言わず** I/O エラー。
         * d2 の先頭ブロックは 1 回の rename のあいだに
         *   1 回目 = 宛先の重複確認 (ext2_find_entry(d2, "moved"))
         *   2 回目 = 循環検査の ext2_parent_of(d2) の ".." 引き
         * と読まれるので、**2 回目だけ**落とす。ずっと落とすと後段の
         * ext2_add_entry も止まってしまい、循環検査を素通りした場合の
         * 被害 (木が輪になる) が見えない。 */
        blk0_lba = lba_of_block0(d2);
        fail_arm(blk0_lba, 2);
        rc = ext2_rename(g_ec, root, "d1", d2, "moved");
        fail_disarm();
        fault_done();
        CHECK(g_fail_fired == 1);
        CHECK(rc == EXT2_ERR_IO);
        CHECK(rc != EXT2_OK);
        /* 木が輪になっていない — /d1 は root の下のまま */
        {
            u32 chk = 0;
            memo_cold();
            CHECK(ext2_lookup(g_ec, "/d1/d2", &chk) == EXT2_OK);
            CHECK(chk == d2);
        }
    }
}

/* ======================================================================== */
/*  段 A2: Codex 実装レビュー P1-1 / P1-2 / P1-3 / P1-5 の反例              */
/*                                                                          */
/*  どれも「検索や更新の**失敗を二値に潰した**次の段」で、前回の修正が       */
/*  届いていなかったところ。Codex は 32bit バイナリを Unicorn で実行して     */
/*  実際に再現している。同じ形をここに入れる。                              */
/* ======================================================================== */

/* "/big" だけ記憶を温める — パス解決は通り、その先の検索が失敗する状態 */
static void warm_big(void)
{
    OS32_Stat st;
    memo_cold();
    CHECK(ext2_vfs_stat(g_ec, "/big", &st) == VFS_OK);
}

/* P1-1: mkdir が「存在確認が読めなかった」まま作らない。
 * 直す前: `if (find_entry(...) == EXT2_OK) return EXIST;` なので I/O エラーは
 * すり抜け、既にある /big/keepme と**同名のディレクトリを作っていた**
 * (Codex 実測: 成功を返し 18 セクタ書き込み、同名エントリ 2 件)。 */
static void case_mkdir_existence_failure(void)
{
    int rc, n;
    u32 lba;

    report("  [P1-1] mkdir: 存在確認が読めなければ作らない\n");
    fault_done();   /* 前の case のエラー状態を持ち越さない (票 B8 往復 5) */

    warm_big();
    n = keep_entry_count();
    CHECK(n == 1);
    lba = lba_of_big_indirect();
    io_reset();
    fail_arm(lba, 1);
    rc = vfs_mkdir("/big/keepme");
    fail_disarm();
    fault_done();

    CHECK(g_fail_fired == 1);
    CHECK(rc < 0);
    CHECK(rc != VFS_OK);
    CHECK(rc != VFS_ERR_EXIST);         /* 「既にある」とも言わない */
    CHECK(wr_non_state() == 0);              /* **1 セクタも書いていない** */
    CHECK(keep_entry_count() == 1);     /* 同名エントリが増えていない */
    CHECK(keep_intact());

    /* 回帰: 読めるなら従来どおり EXIST / 作成ができる */
    memo_cold();
    CHECK(vfs_mkdir("/big/keepme") == VFS_ERR_EXIST);
    memo_cold();
    CHECK(vfs_mkdir("/big/newdir") == VFS_OK);
    CHECK(vfs_path_kind("/big/newdir") == VFS_KIND_DIR);
}

/* P1-2: ext2_create が同じ形。
 * (VFS 経由の ext2_vfs_write は前回直したので、ここは関数の契約そのものを
 *  直接叩く。Codex 実測: 成功を返して同名ファイルを二重作成、16 セクタ。) */
static void case_create_existence_failure(void)
{
    int rc;
    u32 big_ino = 0, lba;

    report("  [P1-2] ext2_create: 存在確認が読めなければ作らない\n");
    fault_done();   /* 前の case のエラー状態を持ち越さない (票 B8 往復 5) */

    warm_big();
    CHECK(ext2_lookup(g_ec, "/big", &big_ino) == EXT2_OK);
    lba = lba_of_big_indirect();
    io_reset();
    fail_arm(lba, 1);
    rc = ext2_create(g_ec, big_ino, "keepme", "XX", 2);
    fail_disarm();
    fault_done();

    CHECK(g_fail_fired == 1);
    CHECK(rc == EXT2_ERR_IO);
    CHECK(rc != EXT2_OK);
    CHECK(rc != EXT2_ERR_EXIST);
    CHECK(wr_non_state() == 0);
    CHECK(keep_entry_count() == 1);
    CHECK(keep_intact());

    /* 回帰 */
    CHECK(ext2_create(g_ec, big_ino, "keepme", "XX", 2) == EXT2_ERR_EXIST);
    CHECK(keep_intact());
    CHECK(ext2_create(g_ec, big_ino, "fresh1", "YY", 2) == EXT2_OK);
}

/* P1-3: rename の**宛先**存在確認。
 * 直す前: 置き換えの分岐を丸ごと飛ばし、add_entry が宛先に同名エントリを
 * 二重に作ったうえで delete_entry が移動元の名前を消していた
 * (Codex 実測: 成功を返し 10 セクタ書き込み、**名前が片方だけ消えて二重**)。 */
static void case_rename_dest_failure(void)
{
    int rc;
    u32 lba, tmp = 0;

    report("  [P1-3] rename: 宛先の確認が読めなければ何もしない\n");
    fault_done();   /* 前の case のエラー状態を持ち越さない (票 B8 往復 5) */

    memo_cold();
    CHECK(vfs_path_kind("/etc/plain") == VFS_KIND_FILE);
    warm_big();

    CHECK(keep_entry_count() == 1);
    lba = lba_of_big_indirect();
    io_reset();
    fail_arm(lba, 1);
    rc = vfs_rename("/etc/plain", "/big/keepme");
    fail_disarm();
    fault_done();

    CHECK(g_fail_fired == 1);
    CHECK(rc < 0);
    CHECK(rc != VFS_OK);
    CHECK(wr_non_state() == 0);                       /* 何も書いていない */
    CHECK(keep_entry_count() == 1);              /* 宛先が二重になっていない */
    CHECK(keep_intact());                        /* 宛先の中身もそのまま */
    /* **移動元の名前が残っている** */
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/etc/plain", &tmp) == EXT2_OK);
    CHECK(vfs_path_kind("/etc/plain") == VFS_KIND_FILE);

    /* 回帰: 読めるなら従来どおり (ファイル同士は置き換え) */
    memo_cold();
    CHECK(vfs_rename("/etc/plain", "/etc/plain2") == VFS_OK);
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/etc/plain", &tmp) == EXT2_ERR_NOTFOUND);
    CHECK(vfs_path_kind("/etc/plain2") == VFS_KIND_FILE);
    memo_cold();
    CHECK(vfs_rename("/etc/plain2", "/etc/plain") == VFS_OK);
}

/* P1-5: 追記の inode 更新が失敗したのに成功 (バイト数) を返していた。
 * Codex 実測: 5 バイトのファイルに 4 バイト追記 -> 戻り値 4、
 * 媒体上のサイズは 5 のまま = 追記が見えない。
 *
 * 1 回の ext2_write_stream で、目当ての inode が載るブロックは
 *   1 回目 = 先頭の ext2_read_inode
 *   2 回目 = 最後の ext2_write_inode の read-modify-write
 * と読まれる。その **2 回目だけ**を落とす。 */
static void case_write_stream_inode_failure(void)
{
    u32 ino = 0, lba, sz = 0;
    int rc;

    report("  [P1-5] 追記: inode を書けなければ成功と言わない\n");
    fault_done();   /* 前の case のエラー状態を持ち越さない (票 B8 往復 5) */

    CHECK(ext2_vfs_write(g_ec, "/etc/small", "01234", 5) == VFS_OK);
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/etc/small", &ino) == EXT2_OK);
    /* 記憶を温めてパス解決が inode 表を読み直さないようにする */
    {
        OS32_Stat st;
        CHECK(ext2_vfs_stat(g_ec, "/etc/small", &st) == VFS_OK);
    }

    lba = lba_of_inode(ino);
    io_reset();
    fail_arm(lba, 2);
    rc = ext2_vfs_write_stream(g_ec, "/etc/small", "ABCD", 4, 5);
    fail_disarm();
    fault_done();

    CHECK(g_fail_fired == 1);
    CHECK(rc < 0);                       /* 4 (成功) と言わない */
    CHECK(rc != 4);
    /* **戻り値が成功でないなら、媒体上のサイズと食い違っていてよい**。
     * 逆に「成功なら一致する」ことを次で押さえる。 */
    CHECK(ext2_vfs_get_size(g_ec, "/etc/small", &sz) == VFS_OK);
    CHECK(sz == 5);

    /* 回帰: 同じ追記をやり直すと通り、**戻り値と媒体上のサイズが一致する** */
    rc = ext2_vfs_write_stream(g_ec, "/etc/small", "ABCD", 4, 5);
    CHECK(rc == 4);
    CHECK(ext2_vfs_get_size(g_ec, "/etc/small", &sz) == VFS_OK);
    CHECK(sz == 9);
    {
        static u8 got[32];
        kmemset(got, 0, sizeof(got));
        CHECK(ext2_read_file(g_ec, ino, got, sizeof(got)) == 9);
        CHECK(kstrncmp((const char *)got, "01234ABCD", 9) == 0);
    }
}

/* ブロックがまだ「使用中」かをビットマップで直に見る
 * (fs/ext2_inode.c の ext2_alloc_block と同じ式)。
 * **解放したかどうかは inode を見ても分からない** — 呼び手が失敗して
 * inode を書き戻さなければ媒体上の inode は元のままだからである。
 * 漏れ (解放したのに誰も指していない) を捕まえるにはここを見るしかない。 */
static int block_in_use(u32 blk)
{
    static u8 bm[EXT2_BLOCK_SIZE];
    u32 rel, g, bit, byte_idx, bit_idx;

    if (blk < g_ec->sb_info.first_data_block) return 1;
    rel = blk - g_ec->sb_info.first_data_block;
    g = rel / g_ec->sb_info.blocks_per_group;
    bit = rel % g_ec->sb_info.blocks_per_group;
    if (g >= g_ec->num_groups) return 1;
    if (ext2_read_block(g_ec, g_ec->gd_table[g].block_bitmap, bm) != 0) return 1;
    byte_idx = bit / 8;
    bit_idx = bit % 8;
    return (bm[byte_idx] & (1 << bit_idx)) ? 1 : 0;
}

/* ======================================================================== */
/*  往復 3: **媒体の状態で**判定する — 相互リンク検査                        */
/* ======================================================================== */

/* 戻り値ではなく媒体の状態を見る検査。**ext2 のコードを通さず RAM ディスク
 * (g_disk) を直に読む** — 検査対象と同じ読み方をすると同じ誤りを共有して
 * 見逃すため。形 (グループ記述子の位置など) だけ g_ec から借りる (操作では
 * 変わらない)。
 *
 *   freed_ref   … 使用中の inode から辿れるのに、ビットマップ上は空きのブロック
 *                 = **次の割り当てで別ファイルと共有される** (相互リンクの前段)
 *   dup_ref     … 2 か所から指されるブロック (相互リンクそのもの)
 *   bad_ref     … 範囲外のブロック番号 (ゴミの表を指している)
 *   dangling    … 空きの inode を指す名前 (inode 版の相互リンクの前段)
 *   unref_inuse … ビットマップ上は使用中だが誰も指さないブロック
 *                 (メタデータ + **漏れ**。操作前との差を漏れとして数える)
 *
 * 往復 4 で足した (レビューの盲点 (a)(b)):
 *   links_short   … その inode を指す名前の数 > links_count
 *                   = **片方の unlink で残りの名前が解放済み inode を指す** (不整合)
 *   links_surplus … 名前の数 < links_count (孤児側。許容して数える)
 *   dir_overrun   … ディレクトリの size より先のブロックが繋がっている (不整合。
 *                   bmap で走査する find_entry / list_dir には見え、size で見る
 *                   道具には見えない — X1 では size=13312 のまま 14 本繋がった)
 *   dir_hole      … size の内側の末尾が繋がっていない (許容して数える。
 *                   ext2_add_entry が size を先に伸ばしてから繋ぐため)
 *
 *
 * 往復 5 で足した (レビューの盲点: 02_two_names と 04_loop を整合と判定していた):
 *   dir_multi   … ディレクトリ inode を指す "." ".." 以外の名前が 2 つ以上
 *                 (links は合っていても OS32 の rmdir は links を見ないので、
 *                  片方の rmdir で残りの名前が解放済み inode を指す)
 *   dotdot_bad  … 名前で辿れる (= 根から名前の鎖が届く) ディレクトリの ".." が、
 *                 そのディレクトリを名前で持つ親を指していない (孤児は対象外)
 *   dir_loop    … 名前の親を辿ると輪に入るディレクトリの数
 *   bad_dir     … 壊れた rec_len (8 未満、4 の倍数でない、ブロックを越える、
 *                 名前が入らない、鎖がブロック末尾でちょうど閉じない)。
 *                 以前は黙って break していた
 *
 * freed_ref / dup_ref / bad_ref / dangling / links_short / dir_overrun /
 * dir_multi / dotdot_bad / dir_loop / bad_dir が 0 であることを
 * 「整合している」と言う。**漏れと孤児は許容して数える。**
 *
 * **この判定は本物の e2fsck -fn と突き合わせる** (test_b8_open.py、抜き取り)。
 * 食い違ったら試験の失敗にする — 自前の検査の盲点を後追いで埋めるのをやめるため。 */
typedef struct {
    u32 freed_ref;
    u32 dup_ref;
    u32 bad_ref;
    u32 dangling;
    u32 unref_inuse;
    u32 links_short;
    u32 links_surplus;
    u32 dir_overrun;
    u32 dir_hole;
    u32 dir_multi;
    u32 dotdot_bad;
    u32 dir_loop;
    u32 bad_dir;
} MediaReport;

#define MR_MAX_INODES  8192
static u16 g_names[MR_MAX_INODES + 1];
/* 往復 5: "." ".." 以外の名前の数 / その名前を持つ親 / 自分の ".." */
static u16 g_dnames[MR_MAX_INODES + 1];
static u32 g_name_parent[MR_MAX_INODES + 1];
static u32 g_dotdot[MR_MAX_INODES + 1];
static u32 g_mr_owner;           /* 走査中のディレクトリの inode 番号 */
static u32 g_mr_top;             /* 走査中のディレクトリの「繋がった最後の位置 + 1」 */

static u8 g_refmap[(DISK_GROUPS_FS_SECTORS / 2 + 7) / 8];
static MediaReport *g_mr;

static u8 *raw_blk(u32 b) { return g_disk + (g_ec->base_lba + b * 2) * 512u; }

static int raw_block_used(u32 b)
{
    u32 rel = b - g_ec->sb_info.first_data_block;
    u32 g = rel / g_ec->sb_info.blocks_per_group;
    u32 bit = rel % g_ec->sb_info.blocks_per_group;
    const u8 *bm = raw_blk(g_ec->gd_table[g].block_bitmap);
    return (bm[bit / 8] >> (bit % 8)) & 1;
}

static int raw_inode_used(u32 ino)
{
    u32 g = (ino - 1) / g_ec->sb_info.inodes_per_group;
    u32 rel = (ino - 1) % g_ec->sb_info.inodes_per_group;
    const u8 *bm = raw_blk(g_ec->gd_table[g].inode_bitmap);
    return (bm[rel / 8] >> (rel % 8)) & 1;
}

static const u8 *raw_inode(u32 ino)
{
    u32 g = (ino - 1) / g_ec->sb_info.inodes_per_group;
    u32 idx = (ino - 1) % g_ec->sb_info.inodes_per_group;
    u32 blk = g_ec->gd_table[g].inode_table
              + (idx * g_ec->sb_info.inode_size) / EXT2_BLOCK_SIZE;
    u32 off = (idx * g_ec->sb_info.inode_size) % EXT2_BLOCK_SIZE;
    return raw_blk(blk) + off;
}

static u32 raw_inode_ptr(u32 ino, int i) { return *(const u32 *)(raw_inode(ino) + 40 + i * 4); }
static u32 raw_links(u32 ino) { return *(const u16 *)(raw_inode(ino) + 26); }

/* 媒体を直に読んで dir_ino の中の name を探す (票 H2 X3)。**ext2 のコードを
 * 通さない** — 検査対象と同じ読み方をすると同じ誤りを共有して見逃す。
 * 直接ブロックだけ見る (この試験のディレクトリは 12 ブロックに収まる)。
 * 戻り値 1 = 見つけた (*out_ino) / 0 = 無い。 */
static int raw_find_name(u32 dir_ino, const char *name, u32 *out_ino)
{
    u32 bi, pos, nlen = h_strlen(name);

    for (bi = 0; bi < EXT2_NDIR_BLOCKS; bi++) {
        u32 phys = raw_inode_ptr(dir_ino, (int)bi);
        u8 *d;
        if (phys == 0) continue;
        if (phys < g_ec->sb_info.first_data_block ||
            phys >= g_ec->sb_info.total_blocks) continue;
        d = raw_blk(phys);
        pos = 0;
        while (pos + 8 <= EXT2_BLOCK_SIZE) {
            u32 ino = *(u32 *)(d + pos);
            u16 rl = *(u16 *)(d + pos + 4);
            u8  nl = d[pos + 6];
            if (rl < 8 || (u32)rl % 4 != 0 || pos + rl > EXT2_BLOCK_SIZE) break;
            if (ino != 0 && nl == (u8)nlen && pos + 8 + nlen <= EXT2_BLOCK_SIZE &&
                kstrncmp(name, (const char *)(d + pos + 8), nlen) == 0) {
                if (out_ino) *out_ino = ino;
                return 1;
            }
            pos += rl;
        }
    }
    return 0;
}

static int mr_ref(u32 b)
{
    if (b < g_ec->sb_info.first_data_block || b >= g_ec->sb_info.total_blocks) {
        g_mr->bad_ref++;
        return 0;
    }
    if (!raw_block_used(b)) g_mr->freed_ref++;
    if (g_refmap[b / 8] & (1 << (b % 8))) g_mr->dup_ref++;
    g_refmap[b / 8] |= (u8)(1 << (b % 8));
    return 1;
}

static int raw_inode_is_dir(u32 ino);

static void mr_dir_block(u32 b)
{
    const u8 *d = raw_blk(b);
    u32 pos = 0;
    while (pos + 8 <= EXT2_BLOCK_SIZE) {
        u32 ino = *(const u32 *)(d + pos);
        u16 rl = *(const u16 *)(d + pos + 4);
        u8 nl = d[pos + 6];
        int dot, dotdot;
        /* 往復 5: 壊れた rec_len を黙って打ち切らず、不整合として数える */
        if (rl < 8 || (rl % 4) != 0 || pos + rl > EXT2_BLOCK_SIZE ||
            (ino != 0 && (u32)nl + 8u > rl)) {
            g_mr->bad_dir++;
            return;
        }
        dot = (nl == 1 && d[pos + 8] == '.');
        dotdot = (nl == 2 && d[pos + 8] == '.' && d[pos + 9] == '.');
        if (ino != 0 && (ino > g_ec->sb_info.total_inodes || !raw_inode_used(ino)))
            g_mr->dangling++;
        if (ino != 0 && ino <= g_ec->sb_info.total_inodes && ino <= MR_MAX_INODES) {
            g_names[ino]++;
            if (dotdot) {
                if (g_mr_owner <= MR_MAX_INODES) g_dotdot[g_mr_owner] = ino;
            } else if (!dot && raw_inode_used(ino) && raw_inode_is_dir(ino)) {
                g_dnames[ino]++;
                g_name_parent[ino] = g_mr_owner;
            }
        }
        pos += rl;
    }
    /* 鎖がブロック末尾でちょうど閉じない */
    if (pos != EXT2_BLOCK_SIZE) g_mr->bad_dir++;
}

static void mr_leaf(u32 b, int is_dir, u32 fidx)
{
    if (mr_ref(b) && is_dir) mr_dir_block(b);
    if (fidx + 1 > g_mr_top) g_mr_top = fidx + 1;
}

static int raw_inode_is_dir(u32 ino)
{
    u16 mode = *(const u16 *)raw_inode(ino);
    return (mode & EXT2_S_IFMT) == EXT2_S_IFDIR;
}

static void mr_walk_inode(u32 ino)
{
    const u8 *in = raw_inode(ino);
    u16 mode = *(const u16 *)in;
    int is_dir = ((mode & EXT2_S_IFMT) == EXT2_S_IFDIR);
    u32 blk[EXT2_N_BLOCKS];
    u32 j, k;
    int i;

    g_mr_owner = ino;
    g_mr_top = 0;
    for (i = 0; i < EXT2_N_BLOCKS; i++) blk[i] = *(const u32 *)(in + 40 + i * 4);
    for (i = 0; i < EXT2_NDIR_BLOCKS; i++) if (blk[i]) mr_leaf(blk[i], is_dir, (u32)i);

    if (blk[EXT2_IND_BLOCK] && mr_ref(blk[EXT2_IND_BLOCK])) {
        const u8 *t = raw_blk(blk[EXT2_IND_BLOCK]);
        for (j = 0; j < EXT2_ADDR_PER_BLOCK; j++) {
            u32 e = *(const u32 *)(t + j * 4);
            if (e) mr_leaf(e, is_dir, EXT2_NDIR_BLOCKS + j);
        }
    }
    if (blk[EXT2_DIND_BLOCK] && mr_ref(blk[EXT2_DIND_BLOCK])) {
        const u8 *t = raw_blk(blk[EXT2_DIND_BLOCK]);
        for (j = 0; j < EXT2_ADDR_PER_BLOCK; j++) {
            u32 ind1 = *(const u32 *)(t + j * 4);
            if (ind1 && mr_ref(ind1)) {
                const u8 *t2 = raw_blk(ind1);
                for (k = 0; k < EXT2_ADDR_PER_BLOCK; k++) {
                    u32 e = *(const u32 *)(t2 + k * 4);
                    if (e) mr_leaf(e, is_dir,
                                   EXT2_NDIR_BLOCKS + EXT2_ADDR_PER_BLOCK
                                   + j * EXT2_ADDR_PER_BLOCK + k);
                }
            }
        }
    }
    if (blk[EXT2_TIND_BLOCK]) (void)mr_ref(blk[EXT2_TIND_BLOCK]);

    if (is_dir) {
        u32 size_blocks = *(const u32 *)(in + 4) / EXT2_BLOCK_SIZE;
        if (g_mr_top > size_blocks) g_mr->dir_overrun++;
        else if (g_mr_top < size_blocks) g_mr->dir_hole++;
    }
}

static void media_check(MediaReport *r)
{
    u32 ino, b;
    kmemset(r, 0, sizeof(*r));
    kmemset(g_refmap, 0, sizeof(g_refmap));
    kmemset(g_names, 0, sizeof(g_names));
    kmemset(g_dnames, 0, sizeof(g_dnames));
    kmemset(g_name_parent, 0, sizeof(g_name_parent));
    kmemset(g_dotdot, 0, sizeof(g_dotdot));
    g_mr = r;
    if (g_ec->sb_info.total_inodes > MR_MAX_INODES) {
        report("  (harness) too many inodes for media_check\n");
        g_failures++;
        return;
    }
    for (ino = 1; ino <= g_ec->sb_info.total_inodes; ino++) {
        if (raw_inode_used(ino)) mr_walk_inode(ino);
    }
    /* (a) 名前の数と links_count */
    for (ino = 1; ino <= g_ec->sb_info.total_inodes; ino++) {
        u16 links;
        if (!raw_inode_used(ino)) continue;
        links = *(const u16 *)(raw_inode(ino) + 26);
        if (g_names[ino] > links) r->links_short++;
        else if (g_names[ino] < links) r->links_surplus++;
    }
    for (b = g_ec->sb_info.first_data_block; b < g_ec->sb_info.total_blocks; b++) {
        if (raw_block_used(b) && !(g_refmap[b / 8] & (1 << (b % 8)))) r->unref_inuse++;
    }

    /* 往復 5: ディレクトリの名前・".."・輪 */
    for (ino = 1; ino <= g_ec->sb_info.total_inodes; ino++) {
        u32 cur, steps;
        int reach_root = 0;
        if (!raw_inode_used(ino) || !raw_inode_is_dir(ino)) continue;
        if (g_dnames[ino] > 1) r->dir_multi++;
        if (ino == EXT2_ROOT_INO) {
            if (g_dotdot[ino] != EXT2_ROOT_INO) r->dotdot_bad++;
            continue;
        }
        /* 名前の親を根まで辿る。根に届けば「名前で辿れる」、親の無い
         * ディレクトリで止まれば孤児の枝 (対象外)、回数を使い切れば輪 */
        cur = ino;
        for (steps = 0; steps <= g_ec->sb_info.total_inodes; steps++) {
            u32 par = g_name_parent[cur];
            if (cur == EXT2_ROOT_INO) { reach_root = 1; break; }
            if (par == 0) break;                    /* 孤児の枝 */
            cur = par;
        }
        if (steps > g_ec->sb_info.total_inodes) { r->dir_loop++; continue; }
        if (reach_root && g_dotdot[ino] != g_name_parent[ino]) r->dotdot_bad++;
    }
}

static int media_ok(const MediaReport *r)
{
    return r->freed_ref == 0 && r->dup_ref == 0 && r->bad_ref == 0 && r->dangling == 0 &&
           r->links_short == 0 && r->dir_overrun == 0 &&
           r->dir_multi == 0 && r->dotdot_bad == 0 && r->dir_loop == 0 && r->bad_dir == 0;
}

/* 票 B8 往復 6: **media_check の直後に呼ぶ**。名前を持たない (孤児の) 生きた
 * ディレクトリのうち、".." が parent を指すものの数。rmdir のガード
 * (空なのに links_count > 2 なら断る) が「本当に孤児がいるときだけ」断り、
 * 「孤児がいるなら必ず断る」ことを、媒体から独立に確かめるために使う。 */
static int orphan_dotdot_refs(u32 parent)
{
    u32 ino;
    int n = 0;
    for (ino = 1; ino <= g_ec->sb_info.total_inodes && ino <= MR_MAX_INODES; ino++) {
        if (ino == EXT2_ROOT_INO) continue;
        if (!raw_inode_used(ino) || !raw_inode_is_dir(ino)) continue;
        if (*(const u16 *)(raw_inode(ino) + 26) == 0) continue;   /* links 0 = 手放し済み */
        if (g_dnames[ino] != 0) continue;                         /* 名前がある */
        if (g_dotdot[ino] == parent) n++;
    }
    return n;
}

/* ガードの判定を 1 回ぶん記録する。rmdir の直前に media_check 済みであること。
 *   refused  : この rmdir でガードが断った (kprintf の通知が 1 増えた)
 *   orphans  : parent を ".." で指す孤児の数
 *   surplus  : parent の links_count が、媒体上で parent を指す名前 ("." ".." を
 *              含む) の数より多い (= 多い側に振れている。mkdir が親の links を
 *              先に上げたところで落ちた、など。e2fsck の「ref count が多い」)
 * 整合の条件は 2 つ:
 *   (i)  **健全な** (孤児なし・links が名前の数と一致する) ディレクトリは断らない
 *   (ii) parent を指す孤児がいるなら、rmdir は成功していない (孤児を置き去りにしない)
 * 多い側に振れているだけのとき断るのは決裁どおり (安全側) なので、別に数える。 */
typedef struct {
    int refused, removed, wrong_refuse, orphan_left;
    int refused_orphan, refused_surplus_only, healthy_removed;
} GuardTally;

static void guard_note(GuardTally *g, int rc, int refused, int orphans, int surplus)
{
    if (refused) {
        g->refused++;
        if (orphans > 0) g->refused_orphan++;
        else if (surplus) g->refused_surplus_only++;
        else g->wrong_refuse++;
    }
    if (rc == EXT2_OK) {
        g->removed++;
        if (orphans == 0 && !surplus) g->healthy_removed++;
    }
    if (orphans > 0 && rc == EXT2_OK) g->orphan_left++;
}

/* 親 top の中の name (空であることが期待される親) を rmdir し、同じ名前で
 * mkdir し直す。レビュアーの反例 (往復 6) の後続操作そのもの: 返した inode 番号を
 * mkdir が受け取ると、孤児の ".." が無関係な生きたディレクトリを指す。 */
static void followup_rmdir_mkdir(GuardTally *g, u32 top, const char *name)
{
    MediaReport pre;
    u32 par = 0;
    u8 t = 0;
    int k0, rc, orphans = 0, surplus = 0;

    if (ext2_find_entry(g_ec, top, name, &par, &t) == EXT2_OK && par <= MR_MAX_INODES) {
        media_check(&pre);
        orphans = orphan_dotdot_refs(par);
        surplus = (*(const u16 *)(raw_inode(par) + 26) > g_names[par]);
    }
    k0 = g_kp_rmdir_refused;
    rc = ext2_rmdir(g_ec, top, name);
    guard_note(g, rc, g_kp_rmdir_refused != k0, orphans, surplus);
    (void)ext2_mkdir(g_ec, top, name);
}

static int leak_delta(const MediaReport *after, const MediaReport *before)
{
    return (int)after->unref_inuse - (int)before->unref_inuse;
}

static void report_media(const MediaReport *r)
{
    report("freed_ref="); report_i((int)r->freed_ref);
    report(" dup_ref="); report_i((int)r->dup_ref);
    report(" bad_ref="); report_i((int)r->bad_ref);
    report(" dangling="); report_i((int)r->dangling);
    report(" unref_inuse="); report_i((int)r->unref_inuse);
    report(" links_short="); report_i((int)r->links_short);
    report(" links_surplus="); report_i((int)r->links_surplus);
    report(" dir_overrun="); report_i((int)r->dir_overrun);
    report(" dir_hole="); report_i((int)r->dir_hole);
    report(" dir_multi="); report_i((int)r->dir_multi);
    report(" dotdot_bad="); report_i((int)r->dotdot_bad);
    report(" dir_loop="); report_i((int)r->dir_loop);
    report(" bad_dir="); report_i((int)r->bad_dir);
}

static void check_media_at(const MediaReport *r, int line)
{
    check_at(media_ok(r), "media consistent (freed/dup/bad/dangling/links_short/dir_overrun/"
                          "dir_multi/dotdot_bad/dir_loop/bad_dir == 0)", line);
    if (!media_ok(r)) { report("      "); report_media(r); report("\n"); }
}

#define CHECK_MEDIA(r) check_media_at((r), __LINE__)

/* ======================================================================== */
/*  票 B8 往復 5: 本物の e2fsck -fn との突き合わせ (抜き取り)                 */
/*                                                                          */
/*  RAM ディスクの ext2 部分をファイルへ書き、標準出力に                     */
/*    @@E2FSCK <path> <media_ok 0|1> <label>                                */
/*  を出して**標準入力の 1 行を待つ**。test_b8_open.py がその行を見て         */
/*  /usr/sbin/e2fsck -fn を当て、出力を「許容 (漏れ側)」「不整合」に分類し、  */
/*  media_ok と食い違えば失敗として数えてから 1 行返す。像は毎回同じパスへ   */
/*  上書きするので、ディスクには常に 1 枚しか残らない。                      */
/*  argv[1] が無いとき (e2fsck が無い環境) は何もしない — Python 側が SKIP と */
/*  明示する ([V4])。                                                        */
/* ======================================================================== */

/* "memory" を必ず付ける — read は引数のバッファを書き換える。付けないと GCC は
 * 呼び出しの前後でバッファが変わらないと見なし、下の応答待ちが古いバイトを見続けて
 * 永久に読み続けた (往復 5 の初回、試験全体が止まった)。 */
static int h_sys3(int nr, long a, long b, long c)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(nr), "b"(a), "c"(b), "d"(c)
                     : "memory");
    return r;
}

static int g_e2f_samples;

/* ラベル組み立て (libc が無いので自前) */
static char g_lbl[600];
static u32 g_lbl_n;
static void lbl_reset(void) { g_lbl_n = 0; g_lbl[0] = '\0'; }
static void lbl_s(const char *t)
{
    while (*t && g_lbl_n + 1 < sizeof(g_lbl)) g_lbl[g_lbl_n++] = *t++;
    g_lbl[g_lbl_n] = '\0';
}
static void lbl_i(int v)
{
    char b[16];
    int i = 15;
    u32 u = (u32)(v < 0 ? -v : v);
    b[i] = '\0';
    if (u == 0) b[--i] = '0';
    while (u > 0) { b[--i] = (char)('0' + u % 10); u /= 10; }
    if (v < 0) b[--i] = '-';
    lbl_s(&b[i]);
}

/* 掃引の抜き取り位置 (フィボナッチ: 前の方ほど密に、後ろほど疎に) */
static int e2f_pick(int at)
{
    int a = 1, b = 2;
    if (at == 1) return 1;
    while (b <= at) {
        int c = a + b;
        if (b == at) return 1;
        a = b; b = c;
    }
    return 0;
}

static void e2f_sample(const char *label, const MediaReport *r)
{
    static char path[512];
    static char ack[8];
    const u8 *src = g_disk + DISK_BASE_LBA * 512u;
    u32 left = g_fs_sectors * 512u, i, j;
    int fd, n;

    if (!g_dump_dir) return;
    for (i = 0; g_dump_dir[i] && i < 400; i++) path[i] = g_dump_dir[i];
    for (j = 0; "/b8.img"[j]; j++) path[i++] = "/b8.img"[j];
    path[i] = '\0';

    fd = h_sys3(5, (long)path, 01 | 0100 | 01000, 0644);   /* open O_WRONLY|O_CREAT|O_TRUNC */
    if (fd < 0) { report("  (harness) e2fsck image open failed\n"); g_failures++; return; }
    while (left > 0) {
        n = h_sys3(4, fd, (long)src, (long)left);
        if (n <= 0) { report("  (harness) e2fsck image write failed\n"); g_failures++; break; }
        src += n; left -= (u32)n;
    }
    (void)h_sys3(6, fd, 0, 0);

    report("@@E2FSCK "); report(path); report(media_ok(r) ? " 1 " : " 0 ");
    report(label); report("\n");
    /* 相手が 1 行返すまで待つ */
    n = h_sys3(3, 0, (long)ack, 1);
    while (n == 1 && ack[0] != '\n') n = h_sys3(3, 0, (long)ack, 1);
    g_e2f_samples++;
}

/* 電源断と同じ再マウント: **新しい ctx** を作り、古い ctx は sync せずに捨てる。
 * エラー状態 (メモリ上) はここで消える — 媒体に残ったものだけが後続に効く。 */
static void remount_fresh(void)
{
    Ext2Ctx *nc;
    if (!g_ec) return;
    g_ec->mounted = 0;
    kfree(g_ec);
    nc = (Ext2Ctx *)ext2_vfs_mount(VFS_MOUNT_DEV_ENCODE(VFS_DEV_HD, 0));
    if (!nc) {
        report("  (harness) remount_fresh failed\n");
        g_failures++;
        g_ec = (Ext2Ctx *)0;
        return;
    }
    g_ec = nc;
    mounts[0].fs_ctx = nc;
}

/* メモリ上の状態 (空き数・経路の記憶) を捨てて媒体から読み直す。
 * ext2_unmount は sync する (= 書く) ので、mounted を落としてから mount する。 */
static void remount_cold(void)
{
    int drv = g_ec->drive_num;
    g_ec->mounted = 0;
    if (ext2_mount(g_ec, drv) != EXT2_OK) {
        report("  (harness) remount failed\n");
        g_failures++;
    }
}

static int file_layout(const char *path, u32 *ino, Ext2Inode *fi)
{
    memo_cold();
    if (ext2_lookup(g_ec, path, ino) != EXT2_OK) return 0;
    return ext2_read_inode(g_ec, *ino, fi) == EXT2_OK;
}

/* ======================================================================== */
/*  往復 3: 狙いを定めた回帰試験                                              */
/* ======================================================================== */

/* Codex P1-A の反例そのもの: 下見 (往復 2) の後で間接表の**再読だけ**が落ちる。
 * 往復 2 は「下見 = 1 回目、再読 = 2 回目」で、2 回目を落とすと直接ブロックを
 * 返したあと inode を書かずに戻った (Codex 実測: rc=-1, direct=0, indirect=1)。
 * 往復 3 は下見を持たないので、1 回目と 2 回目の両方を落として見る。
 * どちらでも媒体が整合していること、落ちたなら漏れを報告すること。 */
static void case_reread_after_probe(void)
{
    static u8 pattern[16 * 1024];
    static u8 got[64];
    MediaReport before, after;
    Ext2Inode fi;
    u32 ino = 0, ind, child = 0, i, lba;
    int nth, rc;

    report("  [P1-A] 上書き中に間接表の読み出し (1 回目 / 2 回目) が落ちる\n");
    fault_done();   /* 前の case のエラー状態を持ち越さない (票 B8 往復 5) */
    for (i = 0; i < sizeof(pattern); i++) pattern[i] = (u8)(i * 11 + 3);

    for (nth = 1; nth <= 2; nth++) {
        CHECK(ext2_vfs_write(g_ec, "/etc/leak", pattern, sizeof(pattern)) == VFS_OK);
        CHECK(file_layout("/etc/leak", &ino, &fi));
        ind = fi.block[EXT2_IND_BLOCK];
        CHECK(ind != 0);
        CHECK(ext2_bmap(g_ec, &fi, EXT2_NDIR_BLOCKS, &child) == EXT2_OK);
        lba = g_ec->base_lba + ind * 2;
        media_check(&before);
        CHECK_MEDIA(&before);

        memo_cold();
        fail_arm(lba, nth);
        rc = ext2_vfs_write(g_ec, "/etc/leak", "small", 5);
        fail_disarm();
        fault_done();

        media_check(&after);
        CHECK_MEDIA(&after);                     /* 判定の中心 */

        CHECK(file_layout("/etc/leak", &ino, &fi));
        if (g_fail_fired) {
            CHECK(rc == VFS_ERR_IO);             /* 漏れを「成功」と言わない */
            CHECK(fi.size == 5);                 /* 新しい中身は書けている */
            CHECK(fi.block[EXT2_IND_BLOCK] == 0);
            /* 読めなかった表は配下ごと漏らす (使用中のまま = 配られない) */
            CHECK(block_in_use(ind) == 1);
            CHECK(block_in_use(child) == 1);
            /* 表 1 + 配下 4 (16KB = 直接 12 + 間接 4) */
            CHECK(leak_delta(&after, &before) == 5);
        } else {
            CHECK(rc == VFS_OK);
            CHECK(leak_delta(&after, &before) == 0);
        }
        kmemset(got, 0, sizeof(got));
        CHECK(ext2_read_file(g_ec, ino, got, sizeof(got)) == 5);
        CHECK(kstrncmp((const char *)got, "small", 5) == 0);
    }
}

/* Codex P1-B: ext2_free_block がビットマップの I/O エラーを捨てていた。
 * 直す前: 読み出し失敗は黙って戻り、**書き込み失敗は空き数だけ増やして rc=0**
 * (Codex 実測: rc=0, old block still allocated)。 */
static void case_free_block_bitmap_failure(void)
{
    static u8 pattern[16 * 1024];
    MediaReport before, after;
    Ext2Inode fi;
    u32 bm_lba, fb, fbc, etc_ino = 0, ino = 0, tmp = 0, i;
    int blk, rc;

    report("  [P1-B] ext2_free_block: ビットマップの読み出し失敗・書き込み失敗\n");
    fault_done();   /* 前の case のエラー状態を持ち越さない (票 B8 往復 5) */
    bm_lba = g_ec->base_lba + g_ec->gd_table[0].block_bitmap * 2;

    blk = ext2_alloc_block(g_ec);
    CHECK(blk > 0);
    /* 前半セクタにビットがあること (下の「後半だけ落ちる」の前提) */
    CHECK((u32)blk - g_ec->sb_info.first_data_block < 512u * 8u);
    fb = g_ec->gd_table[0].free_blocks;
    fbc = g_ec->sb_info.free_blocks_count;

    /* 読み出し失敗: 返せなかったと言い、空き数を動かさない */
    fail_arm_always(bm_lba);
    rc = ext2_free_block(g_ec, (u32)blk);
    fail_disarm();
    CHECK(g_fail_fired > 0);
    CHECK(rc == EXT2_ERR_IO);
    CHECK(block_in_use((u32)blk) == 1);
    CHECK(g_ec->gd_table[0].free_blocks == fb);
    CHECK(g_ec->sb_info.free_blocks_count == fbc);

    /* 書き込み失敗 */
    wfail_arm_always(bm_lba);
    rc = ext2_free_block(g_ec, (u32)blk);
    wfail_disarm();
    CHECK(g_wfail_fired > 0);
    CHECK(rc == EXT2_ERR_IO);
    CHECK(block_in_use((u32)blk) == 1);
    CHECK(g_ec->gd_table[0].free_blocks == fb);
    CHECK(g_ec->sb_info.free_blocks_count == fbc);

    /* 後半セクタだけ書けない: ビットは前半にあるので媒体上は消えるが、
     * 成功とは言わず空き数も動かさない (実際より少なく見える = 安全側) */
    wfail_arm_always(bm_lba + 1);
    rc = ext2_free_block(g_ec, (u32)blk);
    wfail_disarm();
    CHECK(g_wfail_fired > 0);
    CHECK(rc == EXT2_ERR_IO);
    CHECK(block_in_use((u32)blk) == 0);
    CHECK(g_ec->gd_table[0].free_blocks == fb);
    /* 返し直しても空き数を二重に増やさない */
    CHECK(ext2_free_block(g_ec, (u32)blk) == EXT2_OK);
    CHECK(g_ec->gd_table[0].free_blocks == fb);

    /* 正常系 */
    blk = ext2_alloc_block(g_ec);
    CHECK(blk > 0);
    fb = g_ec->gd_table[0].free_blocks;
    CHECK(ext2_free_block(g_ec, (u32)blk) == EXT2_OK);
    CHECK(g_ec->gd_table[0].free_blocks == fb + 1);
    CHECK(block_in_use((u32)blk) == 0);

    /* ここまでは入口の拒否を持たない内部関数 (free_block / alloc_block) なので
     * エラー状態のまま続けた。FS 経由の操作の前に再マウントする (票 B8 往復 5) */
    fault_done();

    /* FS 経由: 削除中にブロックビットマップへ 1 本も書けない */
    for (i = 0; i < sizeof(pattern); i++) pattern[i] = (u8)(i * 3 + 9);
    CHECK(ext2_vfs_write(g_ec, "/etc/bmw", pattern, sizeof(pattern)) == VFS_OK);
    CHECK(file_layout("/etc/bmw", &ino, &fi));
    CHECK(ext2_lookup(g_ec, "/etc", &etc_ino) == EXT2_OK);
    media_check(&before);
    wfail_arm_always(bm_lba);
    rc = ext2_unlink(g_ec, etc_ino, "bmw");
    wfail_disarm();
    fault_done();
    media_check(&after);
    CHECK(g_wfail_fired > 0);
    CHECK(rc == EXT2_ERR_IO);                    /* 漏れを報告する */
    CHECK_MEDIA(&after);
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/etc/bmw", &tmp) == EXT2_ERR_NOTFOUND);
    CHECK(raw_inode_used(ino) == 0);             /* inode はもう何も指さない */
    CHECK(leak_delta(&after, &before) == 17);    /* データ 16 + 間接表 1 */
}

/* 空打ち: 書き込みを取り消し記録つきで一度通し、書いた後の配置を読んでから
 * ディスクとメモリ上の状態を元に戻す。 */
static int dry_write_layout(const char *path, const void *data, u32 size, Ext2Inode *out)
{
    u32 ino = 0;
    int ok;
    remount_cold();
    undo_begin();
    memo_cold();
    ok = (ext2_vfs_write(g_ec, path, data, size) == VFS_OK);
    if (ok) ok = file_layout(path, &ino, out);
    undo_rollback();
    remount_cold();
    return ok;
}

/* PM の分析 ②③: 切り詰めの**後で**新しいブロックの書き込みが落ちる。
 * 往復 2 までは旧ブロックを返してから inode を書かずに戻ったので、媒体上の
 * inode が解放済みの旧ブロックを指したまま残った。 */
static void case_rewrite_new_block_failure(void)
{
    static u8 old_pat[16 * 1024];
    static u8 new_pat[20 * 1024];
    MediaReport before, after;
    Ext2Inode fi, lay;
    u32 ino = 0, i, target;
    int which, rc, k;

    report("  [PM 2/3] 切り詰めた後の新しい表 / データの書き込みが落ちる上書き\n");
    fault_done();   /* 前の case のエラー状態を持ち越さない (票 B8 往復 5) */
    for (i = 0; i < sizeof(old_pat); i++) old_pat[i] = (u8)(i * 7 + 1);
    for (i = 0; i < sizeof(new_pat); i++) new_pat[i] = (u8)(i * 13 + 5);

    for (which = 0; which < 2; which++) {
        CHECK(ext2_vfs_write(g_ec, "/etc/rw", old_pat, sizeof(old_pat)) == VFS_OK);
        CHECK(dry_write_layout("/etc/rw", new_pat, sizeof(new_pat), &lay));
        /* ③ = 最初のデータブロック / ② = ext2_bmap_set が作る新しい間接表 */
        target = (which == 0) ? lay.block[0] : lay.block[EXT2_IND_BLOCK];
        CHECK(target != 0);
        media_check(&before);
        CHECK_MEDIA(&before);

        memo_cold();
        wfail_arm_always(g_ec->base_lba + target * 2);
        rc = ext2_vfs_write(g_ec, "/etc/rw", new_pat, sizeof(new_pat));
        wfail_disarm();
        fault_done();
        media_check(&after);

        CHECK(g_wfail_fired > 0);
        CHECK(rc == VFS_ERR_IO);
        CHECK_MEDIA(&after);                     /* 判定の中心 */
        CHECK(file_layout("/etc/rw", &ino, &fi));
        CHECK(fi.size == 0);                     /* 旧内容は戻らない (0 バイト) */
        for (k = 0; k < EXT2_N_BLOCKS; k++) CHECK(fi.block[k] == 0);
        CHECK(leak_delta(&after, &before) == 0); /* 旧も新も全部返せた */
    }
    /* 回帰: 書けるなら書ける */
    CHECK(ext2_vfs_write(g_ec, "/etc/rw", new_pat, sizeof(new_pat)) == VFS_OK);
}

/* Codex P1-C: ext2_create の失敗後始末が解放の失敗を捨てていた。
 * データの書き込みが落ち、**続く後始末の書き込みも全部落ちる** (装置が途中で
 * 消えた) 場合を作る。空打ちで書き込み先の順序を記録し、新しいファイルの
 * 最初のデータブロックへの書き込みから先を全部落とす。 */
static void case_create_cleanup_failure(void)
{
    static u8 pat[20 * 1024];
    MediaReport before, after;
    Ext2Inode lay;
    u32 etc_ino = 0, tmp = 0, new_ino = 0, i, target;
    int rc, k, first_data_write = -1;

    report("  [P1-C] ext2_create: データ書き込みが落ち、後始末の解放も落ちる\n");
    fault_done();   /* 前の case のエラー状態を持ち越さない (票 B8 往復 5) */
    for (i = 0; i < sizeof(pat); i++) pat[i] = (u8)(i * 17 + 2);
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/etc", &etc_ino) == EXT2_OK);

    remount_cold();
    undo_begin();
    g_wlog_n = 0;
    g_wlog_on = 1;
    rc = ext2_create(g_ec, etc_ino, "cc", pat, sizeof(pat));
    g_wlog_on = 0;
    CHECK(rc == EXT2_OK);
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/etc/cc", &new_ino) == EXT2_OK);
    CHECK(ext2_read_inode(g_ec, new_ino, &lay) == EXT2_OK);
    undo_rollback();
    remount_cold();

    target = g_ec->base_lba + lay.block[0] * 2;
    for (k = 0; k < g_wlog_n; k++) {
        if (g_wlog_lba[k] == target) { first_data_write = k + 1; break; }
    }
    CHECK(first_data_write > 0);

    /* (a) データの書き込みだけが一度落ちる: 後始末は通るので漏れ 0、inode も返す */
    media_check(&before);
    sw_arm(first_data_write, 0, SW_KIND_WRITE);
    rc = ext2_create(g_ec, etc_ino, "cc", pat, sizeof(pat));
    sw_disarm();
    media_check(&after);
    CHECK(g_sw_fired == 1);
    CHECK(rc == EXT2_ERR_IO);
    CHECK_MEDIA(&after);
    CHECK(leak_delta(&after, &before) == 0);
    CHECK(raw_inode_used(new_ino) == 0);
    CHECK(block_in_use(lay.block[0]) == 0);
    remount_cold();

    /* (b) 続く後始末の書き込みも全部落ちる */
    media_check(&before);
    sw_arm(first_data_write, 1, SW_KIND_WRITE);
    rc = ext2_create(g_ec, etc_ino, "cc", pat, sizeof(pat));
    sw_disarm();
    media_check(&after);

    CHECK(g_sw_fired > 0);
    CHECK(rc == EXT2_ERR_IO);
    CHECK_MEDIA(&after);                         /* 判定の中心 */
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/etc/cc", &tmp) == EXT2_ERR_NOTFOUND);
    /* 後始末の書き込みも全部落ちたので、ブロック 0 と inode は漏れる。
     * 媒体上の inode は一度も書いていないので、そのブロックを指していない */
    CHECK(block_in_use(lay.block[0]) == 1);
    CHECK(raw_inode_used(new_ino) == 1);
    CHECK(raw_inode_ptr(new_ino, 0) != lay.block[0]);
    CHECK(leak_delta(&after, &before) == 1);
}

/* create の inode 書き込みが**後半セクタだけ**落ちる (inode は前半に載る = 実は
 * 書けている) うえに、後始末の inode 解放 (ビットマップの読み出し) も落ちる。
 * create は「書けたか区別できない」ので何も返さない。ここでブロックを返すと、
 * inode のビットを戻せなかったとき**ビットの立った inode が解放済みブロックを
 * 指す**。掃引は「1 回だけ」か「そこから全部」しか作らないので、離れた 2 か所の
 * 失敗の組み合わせは掃引に出ない — ここで別に押さえる。 */
static void case_create_inode_write_ambiguous(void)
{
    static u8 pat[4 * 1024];
    MediaReport before, after;
    u32 etc_ino = 0, tmp = 0, ino, idx, grp, i;
    int probe, rc;

    report("  [P1-C''] create: inode が載ったか区別できず、inode の解放も落ちる\n");
    fault_done();   /* 前の case のエラー状態を持ち越さない (票 B8 往復 5) */
    for (i = 0; i < sizeof(pat); i++) pat[i] = (u8)(i * 19 + 4);
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/etc", &etc_ino) == EXT2_OK);

    /* 次に配られる inode が 1KB ブロックの**前半セクタ**に載るまで番号を進める
     * (後半に載る番号は割り当てたまま飛ばす = 足場の漏れ) */
    for (;;) {
        probe = ext2_alloc_inode(g_ec);
        if (probe <= 0) { CHECK(probe > 0); return; }
        idx = ((u32)probe - 1) % g_ec->sb_info.inodes_per_group;
        if ((idx * g_ec->sb_info.inode_size) % EXT2_BLOCK_SIZE < 512u) {
            CHECK(ext2_free_inode(g_ec, (u32)probe) == EXT2_OK);
            break;
        }
    }
    ino = (u32)probe;
    grp = (ino - 1) / g_ec->sb_info.inodes_per_group;

    media_check(&before);
    wfail_arm_always(lba_of_inode(ino) + 1);               /* ブロックの後半だけ落ちる */
    fail_arm(g_ec->base_lba + g_ec->gd_table[grp].inode_bitmap * 2, 2);
                                         /* 1 回目 = alloc_inode / 2 回目 = 後始末 */
    rc = ext2_create(g_ec, etc_ino, "amb", pat, sizeof(pat));
    wfail_disarm();
    fault_done();
    fail_disarm();
    fault_done();
    media_check(&after);

    CHECK(g_wfail_fired > 0);
    CHECK(rc == EXT2_ERR_IO);
    CHECK_MEDIA(&after);                         /* 判定の中心 */
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/etc/amb", &tmp) == EXT2_ERR_NOTFOUND);
    CHECK(raw_inode_used(ino) == 1);
    CHECK(raw_inode_ptr(ino, 0) != 0);           /* 前半は書けていた */
    CHECK(block_in_use(raw_inode_ptr(ino, 0)) == 1);
}

/* 削除でブロックを返しきれない (往復 2 の「inode を残す」を往復 3 で見直した)。 */
static void case_unlink_release_failure(void)
{
    static u8 pattern[16 * 1024];
    MediaReport before, after;
    Ext2Inode fi;
    u32 ino = 0, etc_ino = 0, ind, child = 0, tmp = 0, i;
    int rc, k;

    report("  [BONUS-3] unlink: 間接表が読めない -> 参照を外して漏らし、inode は返す\n");
    fault_done();   /* 前の case のエラー状態を持ち越さない (票 B8 往復 5) */
    for (i = 0; i < sizeof(pattern); i++) pattern[i] = (u8)(i * 5 + 7);
    CHECK(ext2_vfs_write(g_ec, "/etc/orphan", pattern, sizeof(pattern)) == VFS_OK);
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/etc", &etc_ino) == EXT2_OK);
    CHECK(file_layout("/etc/orphan", &ino, &fi));
    ind = fi.block[EXT2_IND_BLOCK];
    CHECK(ind != 0);
    CHECK(ext2_bmap(g_ec, &fi, EXT2_NDIR_BLOCKS, &child) == EXT2_OK);
    media_check(&before);

    fail_arm_always(g_ec->base_lba + ind * 2);
    rc = ext2_unlink(g_ec, etc_ino, "orphan");
    fail_disarm();
    fault_done();
    media_check(&after);

    CHECK(g_fail_fired > 0);
    CHECK(rc == EXT2_ERR_IO);                    /* 返しきれなかったと報告 */
    CHECK_MEDIA(&after);
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/etc/orphan", &tmp) == EXT2_ERR_NOTFOUND);
    /* 媒体上の inode はもう何も指していないので、inode は返す */
    for (k = 0; k < EXT2_N_BLOCKS; k++) CHECK(raw_inode_ptr(ino, k) == 0);
    CHECK(raw_inode_used(ino) == 0);
    CHECK(block_in_use(fi.block[0]) == 0);       /* 直接ブロックは返せた */
    CHECK(block_in_use(ind) == 1);               /* 読めない表は配下ごと漏らす */
    CHECK(block_in_use(child) == 1);
    CHECK(leak_delta(&after, &before) == 5);

    /* inode の書き戻し (参照を外す書き込み) 自体が落ちる -> 何も返さない */
    report("  [BONUS-3b] unlink: 参照を外す書き込みが落ちる -> 何も返さず孤児で残す\n");
    CHECK(ext2_vfs_write(g_ec, "/etc/orphan2", pattern, sizeof(pattern)) == VFS_OK);
    CHECK(file_layout("/etc/orphan2", &ino, &fi));
    media_check(&before);
    wfail_arm_always(lba_of_inode(ino));
    rc = ext2_unlink(g_ec, etc_ino, "orphan2");
    wfail_disarm();
    fault_done();
    media_check(&after);
    CHECK(g_wfail_fired > 0);
    CHECK(rc < 0);
    CHECK_MEDIA(&after);
    CHECK(raw_inode_used(ino) == 1);             /* 孤児として残す */
    CHECK(raw_inode_ptr(ino, EXT2_IND_BLOCK) == fi.block[EXT2_IND_BLOCK]);
    CHECK(block_in_use(fi.block[0]) == 1);
    CHECK(block_in_use(fi.block[EXT2_IND_BLOCK]) == 1);
}

static void rm_long_name(char *dst, int i, int len)
{
    int k;
    for (k = 0; k < len; k++) dst[k] = 'n';
    dst[len] = '\0';
    dst[0] = (char)('A' + i / 26);
    dst[1] = (char)('a' + i % 26);
}

/* 間接ブロックを持つ**空の**ディレクトリを作る: 長い名前でブロックを埋めて
 * から全部消す (ext2 はディレクトリを縮めない)。
 * 名前 240 文字 -> rec_len 248 -> 1 ブロック 4 件 -> 60 件で 15 ブロック。 */
static int make_empty_ind_dir(const char *path, u32 *dino)
{
    static char name[256];
    int i;
    if (ext2_vfs_mkdir(g_ec, path) != VFS_OK) return 0;
    memo_cold();
    if (ext2_lookup(g_ec, path, dino) != EXT2_OK) return 0;
    for (i = 0; i < 60; i++) {
        rm_long_name(name, i, 240);
        if (ext2_create(g_ec, *dino, name, "", 0) != EXT2_OK) return 0;
    }
    for (i = 0; i < 60; i++) {
        rm_long_name(name, i, 240);
        if (ext2_unlink(g_ec, *dino, name) != EXT2_OK) return 0;
    }
    return 1;
}

static void case_rmdir_release_failure(void)
{
    MediaReport before, after;
    Ext2Inode di;
    u32 root = 0, dino = 0, ind, lba, nblocks, tmp = 0;
    int rc, nth;

    report("  [BONUS-4] rmdir: 間接表が読めない -> 参照を外して漏らし、inode は返す\n");
    fault_done();   /* 前の case のエラー状態を持ち越さない (票 B8 往復 5) */
    if (!make_empty_ind_dir("/rmbig", &dino)) {
        report("  (harness) rmbig setup failed\n");
        g_failures++;
        return;
    }
    CHECK(ext2_read_inode(g_ec, dino, &di) == EXT2_OK);
    ind = di.block[EXT2_IND_BLOCK];
    CHECK(ind != 0);
    nblocks = di.size / EXT2_BLOCK_SIZE;
    CHECK(nblocks > EXT2_NDIR_BLOCKS);
    lba = g_ec->base_lba + ind * 2;
    CHECK(ext2_lookup(g_ec, "/", &root) == EXT2_OK);
    media_check(&before);

    /* 空判定 (ext2_is_dir_empty) は bi = 12 .. nblocks の各回で間接表を読む
     * (最後の 1 回は「未割当 = 終わり」を知るため)。その次の 1 回が解放時の
     * 読み出し。**解放時だけ**を落とす。 */
    nth = (int)(nblocks - EXT2_NDIR_BLOCKS) + 2;
    fail_arm(lba, nth);
    rc = ext2_rmdir(g_ec, root, "rmbig");
    fail_disarm();
    fault_done();
    media_check(&after);

    CHECK(g_fail_fired == 1);
    CHECK(rc == EXT2_ERR_IO);
    CHECK_MEDIA(&after);
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/rmbig", &tmp) == EXT2_ERR_NOTFOUND);
    CHECK(raw_inode_used(dino) == 0);
    CHECK(block_in_use(di.block[0]) == 0);
    CHECK(block_in_use(ind) == 1);
    /* 表 1 + 配下 (nblocks - 12) */
    CHECK(leak_delta(&after, &before) == (int)(1 + nblocks - EXT2_NDIR_BLOCKS));

    report("  [BONUS-4b] rmdir: 参照を外す書き込みが落ちる -> 何も返さず孤児で残す\n");
    if (!make_empty_ind_dir("/rmbig2", &dino)) {
        report("  (harness) rmbig2 setup failed\n");
        g_failures++;
        return;
    }
    CHECK(ext2_read_inode(g_ec, dino, &di) == EXT2_OK);
    CHECK(lba_of_inode(dino) != lba_of_inode(root));  /* 親の inode 更新は巻き込まない */
    media_check(&before);
    wfail_arm_always(lba_of_inode(dino));
    rc = ext2_rmdir(g_ec, root, "rmbig2");
    wfail_disarm();
    fault_done();
    media_check(&after);
    CHECK(g_wfail_fired > 0);
    CHECK(rc < 0);
    CHECK_MEDIA(&after);
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/rmbig2", &tmp) == EXT2_ERR_NOTFOUND);
    CHECK(raw_inode_used(dino) == 1);            /* 孤児として残す */
    CHECK(raw_inode_ptr(dino, EXT2_IND_BLOCK) == di.block[EXT2_IND_BLOCK]);
    CHECK(block_in_use(di.block[EXT2_IND_BLOCK]) == 1);
    CHECK(leak_delta(&after, &before) == 0);     /* 孤児から辿れる */
}

/* ======================================================================== */
/*  段 C: 失敗の位置の総当たり                                               */
/* ======================================================================== */

/* 二重間接まで届く大きさ (直接 12 + 単一間接 256 + 二重間接 3) */
#define SW_BIG_BLOCKS   (EXT2_NDIR_BLOCKS + EXT2_ADDR_PER_BLOCK + 3)
#define SW_BIG_BYTES    (SW_BIG_BLOCKS * EXT2_BLOCK_SIZE - 524u)
/* 追記の起点: 直接ブロックの最後 / 単一間接の最後の端数 */
#define SW_APP1_BYTES   (EXT2_NDIR_BLOCKS * EXT2_BLOCK_SIZE - 24u)
#define SW_APP2_BYTES   ((EXT2_NDIR_BLOCKS + EXT2_ADDR_PER_BLOCK) * EXT2_BLOCK_SIZE - 200u)
#define SW_APPEND_BYTES (3u * EXT2_BLOCK_SIZE)
#define SWEEP_MAX       20000
#define DENT_NAME_LEN   250           /* rec_len 260 -> 1 ブロック 3 件 */
#define DENT_FULL       36            /* 直接 12 ブロックがちょうど埋まる件数 */
#define DENT_FULL_IND   39            /* 13 ブロック (単一間接 1 本目まで) がちょうど埋まる件数 */

static u8 g_sw_pat[SW_BIG_BYTES];
static u32 g_sw_dir;
static u32 g_dent_dir;
static char g_dent_name[256];
static u32 g_dent_dir2;
static char g_dent_name2[256];

static int op_overwrite_big(void)
{ return ext2_vfs_write(g_ec, "/sw/big", g_sw_pat, 20u * EXT2_BLOCK_SIZE); }
static int op_grow(void)
{ return ext2_vfs_write(g_ec, "/sw/small", g_sw_pat, SW_BIG_BYTES); }
static int op_create_big(void)
{ return ext2_create(g_ec, g_sw_dir, "newbig", g_sw_pat, SW_BIG_BYTES); }
static int op_unlink_big(void)
{ return ext2_unlink(g_ec, g_sw_dir, "big"); }
static int op_rmdir(void)
{ return ext2_rmdir(g_ec, g_sw_dir, "rmd"); }
static int op_append_ind(void)
{ return ext2_vfs_write_stream(g_ec, "/sw/app1", g_sw_pat, SW_APPEND_BYTES, SW_APP1_BYTES); }
static int op_append_dind(void)
{ return ext2_vfs_write_stream(g_ec, "/sw/app2", g_sw_pat, SW_APPEND_BYTES, SW_APP2_BYTES); }
static int op_create_dent(void)
{ return ext2_create(g_ec, g_dent_dir, g_dent_name, "x", 1); }
static int op_create_dent2(void)
{ return ext2_create(g_ec, g_dent_dir2, g_dent_name2, "x", 1); }
static int op_mkdir(void)
{ return ext2_mkdir(g_ec, g_sw_dir, "newdir"); }

typedef int (*SweepOp)(void);
static const char *g_sw_pattern_name = "";

/* 票 B8 往復 6: 失敗の後に**再起動 (再マウント)** して実行する後続操作。
 * NULL なら従来の掃引。後続操作のガードの判定は g_sw_gt に貯まる。 */
static SweepOp g_sw_follow;
static GuardTally g_sw_gt;

/* 票 H2 X3: 掃引の 1 回ごとに見る**追加の**不変条件。NULL なら見ない。
 * 置き換え rename では「宛先の名前がどの試行でも存在し、旧 inode か
 * 新 inode を指す」を媒体から直に確かめる。 */
static void (*g_sw_extra)(void);

/* op を「at 回目のセクタ I/O が落ちる」形で at = 1, 2, ... と全位置で動かす。
 * sticky = 0 (その 1 回だけ) と 1 (そこから先すべて) の両方。1 回ごとに
 *   - 媒体を検査し (相互リンクの前段が 0 か)、漏れを数え、
 *   - 取り消し記録でディスクを戻し、メモリ上の状態も読み直す。
 * must_report_leak: 漏れたのに成功 (rc >= 0) を返したら失敗とする。 */
static int g_sw_pattern_idx;       /* 0 = 番号の模様 (抜き取りを多めに), 1 = ディレクトリ風 */

static void sweep(const char *label, SweepOp op, int must_report_leak)
{
    MediaReport base, r, first_bad, first_bad_after;
    int sticky, at, rc, fired, pick;
    int s_leak, s_orphan, s_hole, s_bad;
    int bad_after = 0, first_bad_after_at = 0, first_bad_after_sticky = 0;
    int runs = 0, bad = 0, first_bad_at = 0, first_bad_sticky = 0;
    int unreported = 0, first_unrep_at = 0, first_unrep_sticky = 0;
    int leak_runs = 0, err_runs = 0, orphan_runs = 0, hole_runs = 0;
    u32 leak_max = 0;

    kmemset(&first_bad, 0, sizeof(first_bad));
    kmemset(&first_bad_after, 0, sizeof(first_bad_after));
    kmemset(&g_sw_gt, 0, sizeof(g_sw_gt));
    remount_cold();
    media_check(&base);
    CHECK_MEDIA(&base);

    for (sticky = 0; sticky < 2; sticky++) {
        s_leak = s_orphan = s_hole = s_bad = 0;
        for (at = 1; ; at++) {
            if (at > SWEEP_MAX) {
                report("  (harness) sweep too long\n");
                g_failures++;
                break;
            }
            undo_begin();
            remount_cold();
            sw_arm(at, sticky, SW_KIND_ANY);
            rc = op();
            fired = g_sw_fired;
            sw_disarm();

            media_check(&r);
            if (g_sw_extra) g_sw_extra();
            if (!media_ok(&r)) {
                if (!bad) { first_bad = r; first_bad_at = at; first_bad_sticky = sticky; }
                bad++;
            }
            if (rc < 0) err_runs++;
            if (r.links_surplus > base.links_surplus) orphan_runs++;
            if (r.dir_hole > base.dir_hole) hole_runs++;

            /* 票 B8 往復 5: e2fsck の抜き取り。番号の模様ではフィボナッチ位置、
             * 両模様で「最初の漏れ / 孤児 / 穴 / 不整合」の回 */
            pick = (g_sw_pattern_idx == 0 && e2f_pick(at));
            if (!s_leak && r.unref_inuse > base.unref_inuse) { s_leak = 1; pick = 1; }
            if (!s_orphan && r.links_surplus > base.links_surplus) { s_orphan = 1; pick = 1; }
            if (!s_hole && r.dir_hole > base.dir_hole) { s_hole = 1; pick = 1; }
            if (!s_bad && !media_ok(&r)) { s_bad = 1; pick = 1; }
            if (pick) {
                lbl_reset(); lbl_s("SWEEP "); lbl_s(label); lbl_s(" / ");
                lbl_s(g_sw_pattern_name); lbl_s(" at="); lbl_i(at);
                lbl_s(sticky ? " sticky" : " once");
                e2f_sample(g_lbl, &r);
            }
            if (r.unref_inuse > base.unref_inuse) {
                u32 d = r.unref_inuse - base.unref_inuse;
                leak_runs++;
                if (d > leak_max) leak_max = d;
                if (must_report_leak && rc >= 0) {
                    if (!unreported) { first_unrep_at = at; first_unrep_sticky = sticky; }
                    unreported++;
                }
            }

            /* 票 B8 往復 6: 再起動して後続操作 -> もう一度検査 (+ 同じ回で e2fsck) */
            if (g_sw_follow) {
                MediaReport r2;
                remount_cold();              /* エラー状態はメモリ上だけ = 再起動で解ける */
                (void)g_sw_follow();
                media_check(&r2);
                if (!media_ok(&r2)) {
                    if (!bad_after) {
                        first_bad_after = r2; first_bad_after_at = at;
                        first_bad_after_sticky = sticky;
                    }
                    bad_after++;
                }
                if (pick || !media_ok(&r2)) {
                    lbl_reset(); lbl_s("SWEEP "); lbl_s(label); lbl_s(" / ");
                    lbl_s(g_sw_pattern_name); lbl_s(" after follow-ups at="); lbl_i(at);
                    lbl_s(sticky ? " sticky" : " once");
                    e2f_sample(g_lbl, &r2);
                }
            }
            undo_rollback();
            if (g_undo_overflow) {
                report("  (harness) undo log overflow\n");
                g_failures++;
                break;
            }
            runs++;
            if (!fired) break;               /* 最後まで落ちずに通った = 位置を使い切った */
        }
    }
    remount_cold();

    report("  [SWEEP] "); report(label); report(" / "); report(g_sw_pattern_name);
    report("\n          runs="); report_i(runs);
    report(" error-runs="); report_i(err_runs);
    report(" leak-runs="); report_i(leak_runs);
    report(" max-leak="); report_i((int)leak_max);
    report(" orphan-runs="); report_i(orphan_runs);
    report(" hole-runs="); report_i(hole_runs);
    report(" inconsistent="); report_i(bad);
    report(" unreported-leak="); report_i(unreported);
    report("\n");

    check_at(bad == 0, "sweep: media consistent after every injected failure", __LINE__);
    if (bad) {
        report("      first at="); report_i(first_bad_at);
        report(first_bad_sticky ? " (sticky) " : " (once) ");
        report_media(&first_bad); report("\n");
    }
    check_at(unreported == 0, "sweep: a leak is never reported as success", __LINE__);
    if (unreported) {
        report("      first at="); report_i(first_unrep_at);
        report(first_unrep_sticky ? " (sticky)\n" : " (once)\n");
    }

    if (g_sw_follow) {
        report("          follow-ups: inconsistent-after="); report_i(bad_after);
        report(" parent-removed="); report_i(g_sw_gt.removed);
        report(" (healthy="); report_i(g_sw_gt.healthy_removed);
        report(") guard-refused="); report_i(g_sw_gt.refused);
        report(" (orphan="); report_i(g_sw_gt.refused_orphan);
        report(" links-surplus-only="); report_i(g_sw_gt.refused_surplus_only);
        report(") refused-healthy="); report_i(g_sw_gt.wrong_refuse);
        report(" removed-with-orphan="); report_i(g_sw_gt.orphan_left);
        report("\n");
        check_at(bad_after == 0, "sweep: media consistent after reboot + follow-ups", __LINE__);
        if (bad_after) {
            report("      first at="); report_i(first_bad_after_at);
            report(first_bad_after_sticky ? " (sticky) " : " (once) ");
            report_media(&first_bad_after); report("\n");
        }
        check_at(g_sw_gt.wrong_refuse == 0, "sweep: rmdir guard never refuses a healthy directory",
                 __LINE__);
        check_at(g_sw_gt.orphan_left == 0, "sweep: rmdir never frees the parent of an orphan",
                 __LINE__);
        check_at(orphan_runs == 0 || g_sw_gt.refused_orphan > 0,
                 "sweep: the rmdir guard is actually exercised by an orphan", __LINE__);
        check_at(g_sw_gt.healthy_removed > 0, "sweep: a healthy parent is still removed", __LINE__);
    }
}

static void dent_name(char *dst, int k)
{
    int i;
    for (i = 0; i < DENT_NAME_LEN; i++) dst[i] = 'L';
    dst[DENT_NAME_LEN] = '\0';
    dst[0] = (char)('a' + k / 26);
    dst[1] = (char)('a' + k % 26);
}

/* 最後のブロックまでちょうど埋まったディレクトリ。count = DENT_FULL なら
 * 次の 1 件で単一間接表が**新しく**でき、DENT_FULL_IND なら**既にある**
 * 単一間接表へ項目が足される。next_name に次の名前を入れて返す。 */
static int make_full_dir(const char *path, int count, u32 *dino, char *next_name)
{
    Ext2Inode di;
    u32 blocks = (count == DENT_FULL) ? EXT2_NDIR_BLOCKS : EXT2_NDIR_BLOCKS + 1;
    int k;
    if (ext2_vfs_mkdir(g_ec, path) != VFS_OK) return 0;
    memo_cold();
    if (ext2_lookup(g_ec, path, dino) != EXT2_OK) return 0;
    for (k = 0; k < count; k++) {
        dent_name(next_name, k);
        if (ext2_create(g_ec, *dino, next_name, "", 0) != EXT2_OK) return 0;
    }
    if (ext2_read_inode(g_ec, *dino, &di) != EXT2_OK) return 0;
    if (di.size != blocks * EXT2_BLOCK_SIZE) return 0;
    if ((di.block[EXT2_IND_BLOCK] != 0) != (count != DENT_FULL)) return 0;
    dent_name(next_name, count);
    return 1;
}

/* 空きブロックを「もっともらしいブロック番号」で埋める。
 * ext2_format 直後の空きは 0 なので、**書けなかった表を信用する誤り**
 * (ゴミの表を読んで返す・指す) があっても 0 を読んで無害に見えてしまう。
 * 実機の空きには前の持ち主の中身が残っている。ここでは生きているファイル
 * (/big/keepme と /etc/plain) のデータブロックの番号を並べておき、ゴミの表を
 * 信用すると**生きているファイルのブロックを返す**ようにする。 */
static void scribble_free_blocks(void)
{
    Ext2Inode fi;
    u32 ino = 0, victim[2], b, j;

    victim[0] = 0; victim[1] = 0;
    if (file_layout("/big/keepme", &ino, &fi)) victim[0] = fi.block[0];
    if (file_layout("/etc/plain", &ino, &fi)) victim[1] = fi.block[0];
    CHECK(victim[0] != 0 && victim[1] != 0);
    for (b = g_ec->sb_info.first_data_block; b < g_ec->sb_info.total_blocks; b++) {
        u8 *d;
        if (raw_block_used(b)) continue;
        d = raw_blk(b);
        for (j = 0; j < EXT2_ADDR_PER_BLOCK; j++) *(u32 *)(d + j * 4) = victim[j & 1];
    }
}

/* (c) ディレクトリ風のゴミ (レビューの盲点)。空きブロックを「削除済み
 * ディレクトリのブロック」に見える中身で埋める: 先頭に**未使用の inode 番号**を
 * 持つ有効なエントリ "q"、残りは空エントリ。番号の模様はディレクトリとして読むと
 * 使用中の inode 番号に当たって dangling にならず、中身を書く前に繋ぐ誤り (X1)
 * を見逃した。 */
static void scribble_free_blocks_dirlike(u32 phantom)
{
    u32 b;
    CHECK(raw_inode_used(phantom) == 0);
    for (b = g_ec->sb_info.first_data_block; b < g_ec->sb_info.total_blocks; b++) {
        u8 *d;
        if (raw_block_used(b)) continue;
        d = raw_blk(b);
        kmemset(d, 0, EXT2_BLOCK_SIZE);
        *(u32 *)(d + 0) = phantom;
        *(u16 *)(d + 4) = 12;
        d[6] = 1; d[7] = EXT2_FT_REG_FILE; d[8] = 'q';
        *(u32 *)(d + 12) = 0;
        *(u16 *)(d + 16) = (u16)(EXT2_BLOCK_SIZE - 12);
    }
}

/* ---- 配置 (X2): スラックに消したエントリがある状態で、2 セクタ目に載る追加 ----
 * ".", ".." (24B) の後に 244 文字の A, B, C (rec 252) を並べると C は 528 =
 * 2 セクタ目の先頭に載る。C を消し、その inode 番号を別のファイル F が再利用
 * する。次に同じ長さの D を足すと、B の rec_len (1 セクタ目) を縮める書き込みと
 * D の中身 (2 セクタ目) が別セクタに載る。
 * legacy = 1: 往復 4 より前のコードで消した状態 (**スラックに C の inode 番号が
 * 残っている**) を媒体へ直に書いて作る。既存の NHD にはこの状態が残っている。 */
#define ST_NAME_LEN  244
static char g_st_name_d[ST_NAME_LEN + 1];

static void st_name(char *dst, char ch)
{
    int i;
    for (i = 0; i < ST_NAME_LEN; i++) dst[i] = ch;
    dst[ST_NAME_LEN] = '\0';
}

static int make_slack_dir(const char *path, const char *fpath, int legacy,
                          u32 *dino, u32 *c_ino_out)
{
    static char n[ST_NAME_LEN + 1];
    Ext2Inode di;
    u32 c_ino = 0, f_ino = 0, parent = 0;
    int k;
    u8 t = 0;

    if (ext2_vfs_mkdir(g_ec, path) != VFS_OK) return 0;
    memo_cold();
    if (ext2_lookup(g_ec, path, dino) != EXT2_OK) return 0;
    for (k = 0; k < 3; k++) {
        st_name(n, (char)('A' + k));
        if (ext2_create(g_ec, *dino, n, "", 0) != EXT2_OK) return 0;
    }
    st_name(n, 'C');
    if (ext2_find_entry(g_ec, *dino, n, &c_ino, &t) != EXT2_OK) return 0;
    if (ext2_unlink(g_ec, *dino, n) != EXT2_OK) return 0;
    if (ext2_read_inode(g_ec, *dino, &di) != EXT2_OK) return 0;
    {
        u8 *d = raw_blk(di.block[0]);
        if (*(u16 *)(d + 532) == 0) return 0;          /* C の跡がある */
        if (legacy) *(u32 *)(d + 528) = c_ino;          /* 旧コードの削除 */
        /* legacy = 0 のときの「0 になっているか」は呼び手が CHECK で見る
         * (ここで断ると、0 書きを外した変異が媒体検査の前に止まる) */
    }
    if (ext2_vfs_write(g_ec, fpath, "FFFF", 4) != VFS_OK) return 0;
    memo_cold();
    if (ext2_lookup(g_ec, fpath, &f_ino) != EXT2_OK) return 0;
    if (f_ino != c_ino) return 0;                       /* F が C の番号を再利用 */
    (void)parent;
    if (c_ino_out) *c_ino_out = c_ino;
    st_name(g_st_name_d, 'D');
    return 1;
}

static u32 g_st_dir, g_st2_dir, g_etc_dir;
static int op_create_st(void)  { return ext2_create(g_ec, g_st_dir, g_st_name_d, "", 0); }
static int op_create_st2(void) { return ext2_create(g_ec, g_st2_dir, g_st_name_d, "", 0); }
static int op_rename_same(void)
{ return ext2_rename(g_ec, g_sw_dir, "small", g_sw_dir, "moved"); }
static int op_rename_cross(void)
{ return ext2_rename(g_ec, g_sw_dir, "small", g_etc_dir, "moved"); }
/* 置き換えの旧 inode (app1) と新 inode (small)。掃引の不変条件で使う */
static u32 g_x3_replace_old, g_x3_replace_new;
static int op_rename_replace(void)
{ return ext2_rename(g_ec, g_sw_dir, "small", g_sw_dir, "app1"); }
static int op_rename_dir(void)
{ return ext2_rename(g_ec, g_sw_dir, "rmd", g_etc_dir, "rmd2"); }

/* 票 H2 X3: 置き換え rename の掃引に掛ける不変条件。
 * **宛先の名前がどの試行でも存在し、旧 inode D か新 inode S を指す。** */
static u32 g_x3_dir_ino, g_x3_old_ino, g_x3_new_ino;
static const char *g_x3_dst_name;
static int g_x3_runs, g_x3_missing, g_x3_alien, g_x3_new, g_x3_old;

static void x3_check_dst(void)
{
    u32 ino = 0;
    g_x3_runs++;
    if (!raw_find_name(g_x3_dir_ino, g_x3_dst_name, &ino)) {
        g_x3_missing++;
        return;
    }
    if (ino == g_x3_new_ino) g_x3_new++;
    else if (ino == g_x3_old_ino) g_x3_old++;
    else g_x3_alien++;
}

static void x3_begin(u32 dir_ino, const char *dst_name, u32 old_ino, u32 new_ino)
{
    g_x3_dir_ino = dir_ino; g_x3_dst_name = dst_name;
    g_x3_old_ino = old_ino; g_x3_new_ino = new_ino;
    g_x3_runs = g_x3_missing = g_x3_alien = g_x3_new = g_x3_old = 0;
    g_sw_extra = x3_check_dst;
}

static void x3_end(const char *label)
{
    g_sw_extra = (void (*)(void))0;
    report("          [X3] "); report(label);
    report(" runs="); report_i(g_x3_runs);
    report(" dst->new="); report_i(g_x3_new);
    report(" dst->old="); report_i(g_x3_old);
    report(" dst-missing="); report_i(g_x3_missing);
    report(" dst-alien="); report_i(g_x3_alien);
    report("\n");
    check_at(g_x3_missing == 0,
             "X3: the destination name exists after every injected failure",
             __LINE__);
    check_at(g_x3_alien == 0,
             "X3: the destination name points to the old or the new inode",
             __LINE__);
    check_at(g_x3_new > 0 && g_x3_old > 0,
             "X3: both outcomes (old / new) actually occur in the sweep",
             __LINE__);
}

static void run_all_sweeps(void)
{
    sweep("ext2_write 上書き (二重間接 -> 単一間接)", op_overwrite_big, 1);
    sweep("ext2_write 伸長 (4B -> 二重間接)", op_grow, 1);
    sweep("ext2_create (二重間接まで)", op_create_big, 1);
    sweep("ext2_unlink (二重間接つき)", op_unlink_big, 1);
    sweep("ext2_rmdir (間接つきの空ディレクトリ)", op_rmdir, 1);
    sweep("write_stream 追記 (直接 -> 単一間接)", op_append_ind, 0);
    sweep("write_stream 追記 (単一間接 -> 二重間接)", op_append_dind, 0);
    sweep("ext2_create (ディレクトリが単一間接へ伸びる)", op_create_dent, 1);
    sweep("ext2_create (ディレクトリの既存の単一間接に 1 ブロック足す)", op_create_dent2, 1);
    sweep("ext2_mkdir", op_mkdir, 1);
    sweep("ext2_create (スラックに消した跡 / 2 セクタ目に載る)", op_create_st, 1);
    sweep("ext2_create (旧コードの跡 = inode 番号が残る / 2 セクタ目)", op_create_st2, 1);
    sweep("ext2_rename (同じディレクトリ)", op_rename_same, 1);
    sweep("ext2_rename (別のディレクトリへ)", op_rename_cross, 1);
    x3_begin(g_sw_dir, "app1", g_x3_replace_old, g_x3_replace_new);
    sweep("ext2_rename (既存ファイルを置き換える)", op_rename_replace, 1);
    x3_end("ext2_rename (既存ファイルを置き換える)");
    sweep("ext2_rename (ディレクトリを別の親へ)", op_rename_dir, 1);
}

static void stage_c_sweeps(void)
{
    u32 i, rmd = 0;
    int f0 = g_failures;

    report("== 段 C: 失敗の位置を総当たりで動かし、毎回媒体を検査する ==\n");
    disk_setup();
    if (g_failures != f0) { disk_teardown(); return; }

    for (i = 0; i < sizeof(g_sw_pat); i++) g_sw_pat[i] = (u8)(i * 29 + 17);

    CHECK(ext2_vfs_mkdir(g_ec, "/sw") == VFS_OK);
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/sw", &g_sw_dir) == EXT2_OK);
    CHECK(ext2_vfs_write(g_ec, "/sw/big", g_sw_pat, SW_BIG_BYTES) == VFS_OK);
    CHECK(ext2_vfs_write(g_ec, "/sw/small", "tiny", 4) == VFS_OK);
    CHECK(ext2_vfs_write(g_ec, "/sw/app1", g_sw_pat, SW_APP1_BYTES) == VFS_OK);
    CHECK(ext2_vfs_write(g_ec, "/sw/app2", g_sw_pat, SW_APP2_BYTES) == VFS_OK);
    CHECK(make_empty_ind_dir("/sw/rmd", &rmd));
    CHECK(make_full_dir("/sw/dent", DENT_FULL, &g_dent_dir, g_dent_name));
    CHECK(make_full_dir("/sw/dent2", DENT_FULL_IND, &g_dent_dir2, g_dent_name2));
    CHECK(make_slack_dir("/sw/st", "/sw/F", 0, &g_st_dir, (u32 *)0));
    CHECK(make_slack_dir("/sw/st2", "/sw/F2", 1, &g_st2_dir, (u32 *)0));
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/etc", &g_etc_dir) == EXT2_OK);
    CHECK(ext2_lookup(g_ec, "/sw/app1", &g_x3_replace_old) == EXT2_OK);
    CHECK(ext2_lookup(g_ec, "/sw/small", &g_x3_replace_new) == EXT2_OK);
    if (g_failures != f0) { disk_teardown(); return; }

    /* 同じ操作を 2 種類のゴミの模様で回す (盲点 (c)) */
    scribble_free_blocks();
    g_sw_pattern_name = "番号の模様";
    g_sw_pattern_idx = 0;
    run_all_sweeps();

    scribble_free_blocks_dirlike(g_ec->sb_info.total_inodes);   /* 末尾 = 未使用 */
    g_sw_pattern_name = "ディレクトリ風の模様";
    g_sw_pattern_idx = 1;
    run_all_sweeps();

    disk_teardown();
}

/* ======================================================================== */
/*  段 C2 (往復 6): 往復 3/4 の mkdir / rmdir の孤児 -> 再起動 -> 親を rmdir     */
/*  -> 同じ名前で mkdir                                                     */
/* ======================================================================== */

/* 段 C の /sw には他の試験用のファイルが並んでいて空にならないので、孤児の親が
 * **それ以外に何も持たない**木を別に作る (段 C の配置と回数を変えないよう、
 * ディスクも別)。
 *   /iso1/par            … mkdir の掃引 (op: mkdir /iso1/par/newdir)
 *   /iso2/par/rmd        … rmdir の掃引 (op: rmdir /iso2/par/rmd、間接つきの空)
 * 後続操作 (再起動後): 親 par を rmdir -> 同じ名前で mkdir。 */
static u32 g_iso1_top, g_iso1_par, g_iso2_top, g_iso2_par;

static int op_mkdir_iso(void) { return ext2_mkdir(g_ec, g_iso1_par, "newdir"); }
static int op_rmdir_iso(void) { return ext2_rmdir(g_ec, g_iso2_par, "rmd"); }
static int follow_iso1(void) { followup_rmdir_mkdir(&g_sw_gt, g_iso1_top, "par"); return 0; }
static int follow_iso2(void) { followup_rmdir_mkdir(&g_sw_gt, g_iso2_top, "par"); return 0; }

static void stage_c2_orphan_parent_sweeps(void)
{
    u32 rmd = 0;
    int f0 = g_failures;

    report("== 段 C2: mkdir / rmdir の孤児 -> 再起動 -> 孤児の親を rmdir -> 同じ名前で mkdir ==\n");
    fault_done();
    disk_setup();
    if (g_failures != f0) { disk_teardown(); return; }
    CHECK(ext2_vfs_mkdir(g_ec, "/iso1") == VFS_OK);
    CHECK(ext2_vfs_mkdir(g_ec, "/iso1/par") == VFS_OK);
    CHECK(ext2_vfs_mkdir(g_ec, "/iso2") == VFS_OK);
    CHECK(ext2_vfs_mkdir(g_ec, "/iso2/par") == VFS_OK);
    CHECK(make_empty_ind_dir("/iso2/par/rmd", &rmd));
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/iso1", &g_iso1_top) == EXT2_OK);
    CHECK(ext2_lookup(g_ec, "/iso1/par", &g_iso1_par) == EXT2_OK);
    CHECK(ext2_lookup(g_ec, "/iso2", &g_iso2_top) == EXT2_OK);
    CHECK(ext2_lookup(g_ec, "/iso2/par", &g_iso2_par) == EXT2_OK);
    CHECK(ext2_sync(g_ec) == EXT2_OK);
    if (g_failures != f0) { disk_teardown(); return; }

    scribble_free_blocks();
    g_sw_pattern_name = "番号の模様";
    g_sw_pattern_idx = 0;
    g_sw_follow = follow_iso1;
    sweep("ext2_mkdir -> 再起動 -> 親を rmdir / mkdir", op_mkdir_iso, 1);
    g_sw_follow = follow_iso2;
    sweep("ext2_rmdir (間接つき) -> 再起動 -> 親を rmdir / mkdir", op_rmdir_iso, 1);

    scribble_free_blocks_dirlike(g_ec->sb_info.total_inodes);
    g_sw_pattern_name = "ディレクトリ風の模様";
    g_sw_pattern_idx = 1;
    g_sw_follow = follow_iso1;
    sweep("ext2_mkdir -> 再起動 -> 親を rmdir / mkdir", op_mkdir_iso, 1);
    g_sw_follow = follow_iso2;
    sweep("ext2_rmdir (間接つき) -> 再起動 -> 親を rmdir / mkdir", op_rmdir_iso, 1);
    g_sw_follow = (SweepOp)0;

    disk_teardown();
}

/* ======================================================================== */
/*  段 H: **独立した 2 か所**の失敗の組み合わせ (二重故障)                    */
/*                                                                          */
/*  段 C の掃引は「N 番目を 1 回だけ (once)」「N 番目以降を全部 (sticky)」の  */
/*  2 形式しか作らない。TASK_FS_TYPE §2-6 が残していた「独立した 2 か所の     */
/*  失敗」を、代表的な書き込み経路について i < j の全組み合わせで掃く。       */
/*                                                                          */
/*  組み合わせは N^2/2 で増えるので、経路ごとに**空打ちで I/O 数 N を数え**、 */
/*  N が上限を越える経路は理由つきで飛ばす。上限は既定 PAIR_MAXN_DEFAULT で、 */
/*  環境変数 B8_PAIR_MAXN で上げられる (広い掃引はこれで切り替える)。        */
/*                                                                          */
/*  判定は段 C と同じ media_check + 本物の e2fsck -fn の抜き取り。許してよい  */
/*  のは §2-6 の「漏れ」(使用中だが未参照) と孤児・末尾の穴だけ。            */
/* ======================================================================== */

#define PAIR_MAXN_DEFAULT 200
static int g_pair_maxn = PAIR_MAXN_DEFAULT;

/* 13 ブロック = 直接 12 + 単一間接 1 (間接表を 1 本使う「大きいファイル」) */
#define PAIR_BIG_BYTES  ((EXT2_NDIR_BLOCKS + 1u) * EXT2_BLOCK_SIZE - 100u)

static u32 g_p_dir, g_q_dir;

static int op_p_create(void)     { return ext2_create(g_ec, g_p_dir, "new", "x", 1); }
static int op_p_create_big(void) { return ext2_create(g_ec, g_p_dir, "nb", g_sw_pat, PAIR_BIG_BYTES); }
static int op_p_write_big(void)  { return ext2_vfs_write(g_ec, "/p/w", g_sw_pat, PAIR_BIG_BYTES); }
static int op_p_truncate(void)   { return ext2_vfs_write(g_ec, "/p/t", "tiny", 4); }
static int op_p_unlink(void)     { return ext2_unlink(g_ec, g_p_dir, "c1"); }
static int op_p_unlink_big(void) { return ext2_unlink(g_ec, g_p_dir, "big13"); }
static int op_p_rename_same(void)  { return ext2_rename(g_ec, g_p_dir, "mv", g_p_dir, "mv2"); }
static int op_p_rename_cross(void) { return ext2_rename(g_ec, g_p_dir, "mv", g_q_dir, "mv2"); }
static int op_p_rename_dir(void)   { return ext2_rename(g_ec, g_p_dir, "dm", g_q_dir, "dm2"); }
/* 票 H2 X3: 二重故障でも宛先の名前を消さないこと (mv -> c1 を置き換える) */
static u32 g_x3_pair_old, g_x3_pair_new;
static int op_p_rename_replace(void)
{ return ext2_rename(g_ec, g_p_dir, "mv", g_p_dir, "c1"); }
static int op_p_mkdir(void)      { return ext2_mkdir(g_ec, g_p_dir, "nd"); }
static int op_p_rmdir(void)      { return ext2_rmdir(g_ec, g_p_dir, "d"); }

/* 空打ち: 1 つも落とさずに走らせて I/O の回数を数える */
static int pair_count_io(SweepOp op)
{
    int n;
    undo_begin();
    remount_cold();
    sw_arm(SWEEP_MAX + 1, 0, SW_KIND_ANY);
    (void)op();
    n = g_sw_seen;
    sw_disarm();
    undo_rollback();
    remount_cold();
    return n;
}

static void pair_sweep(const char *label, SweepOp op, int must_report_leak)
{
    MediaReport base, r, first_bad;
    int n, i, j, rc, f1, f2, pick;
    int runs = 0, both = 0, bad = 0, first_bad_i = 0, first_bad_j = 0;
    int err_runs = 0, leak_runs = 0, orphan_runs = 0, hole_runs = 0;
    int unreported = 0, first_unrep_i = 0, first_unrep_j = 0;
    int s_leak = 0, s_orphan = 0, s_hole = 0, s_bad = 0;
    u32 leak_max = 0;

    kmemset(&first_bad, 0, sizeof(first_bad));
    remount_cold();
    media_check(&base);
    CHECK_MEDIA(&base);

    n = pair_count_io(op);
    if (n > g_pair_maxn) {
        report("  [PAIR] "); report(label); report(" / "); report(g_sw_pattern_name);
        report("\n         SKIP: I/O 数 N="); report_i(n);
        report(" が上限 "); report_i(g_pair_maxn);
        report(" を越える (B8_PAIR_MAXN で上げられる)\n");
        return;
    }

    for (i = 1; i < n; i++) {
        for (j = i + 1; ; j++) {
            if (j > SWEEP_MAX) {
                report("  (harness) pair sweep too long\n");
                g_failures++;
                break;
            }
            undo_begin();
            remount_cold();
            sw_arm_pair(i, j);
            rc = op();
            f1 = g_sw_fired1;
            f2 = g_sw_fired2;
            sw_disarm();

            media_check(&r);
            if (g_sw_extra) g_sw_extra();
            runs++;
            if (f1 && f2) both++;
            if (rc < 0) err_runs++;
            if (!media_ok(&r)) {
                if (!bad) { first_bad = r; first_bad_i = i; first_bad_j = j; }
                bad++;
            }
            if (r.links_surplus > base.links_surplus) orphan_runs++;
            if (r.dir_hole > base.dir_hole) hole_runs++;

            pick = (g_sw_pattern_idx == 0 && e2f_pick(i) && e2f_pick(j));
            if (!s_leak && r.unref_inuse > base.unref_inuse) { s_leak = 1; pick = 1; }
            if (!s_orphan && r.links_surplus > base.links_surplus) { s_orphan = 1; pick = 1; }
            if (!s_hole && r.dir_hole > base.dir_hole) { s_hole = 1; pick = 1; }
            if (!s_bad && !media_ok(&r)) { s_bad = 1; pick = 1; }
            if (pick && f1 && f2) {
                lbl_reset(); lbl_s("PAIR "); lbl_s(label); lbl_s(" / ");
                lbl_s(g_sw_pattern_name); lbl_s(" i="); lbl_i(i);
                lbl_s(" j="); lbl_i(j);
                e2f_sample(g_lbl, &r);
            }
            if (r.unref_inuse > base.unref_inuse) {
                u32 d = r.unref_inuse - base.unref_inuse;
                leak_runs++;
                if (d > leak_max) leak_max = d;
                if (must_report_leak && rc >= 0) {
                    if (!unreported) { first_unrep_i = i; first_unrep_j = j; }
                    unreported++;
                }
            }

            undo_rollback();
            if (g_undo_overflow) {
                report("  (harness) undo log overflow\n");
                g_failures++;
                break;
            }
            if (!f2) break;          /* j がこの走行の I/O 数を越えた */
        }
    }
    remount_cold();

    report("  [PAIR] "); report(label); report(" / "); report(g_sw_pattern_name);
    report("\n         N="); report_i(n);
    report(" runs="); report_i(runs);
    report(" both-fired="); report_i(both);
    report(" error-runs="); report_i(err_runs);
    report(" leak-runs="); report_i(leak_runs);
    report(" max-leak="); report_i((int)leak_max);
    report(" orphan-runs="); report_i(orphan_runs);
    report(" hole-runs="); report_i(hole_runs);
    report(" inconsistent="); report_i(bad);
    report(" unreported-leak="); report_i(unreported);
    report("\n");

    check_at(bad == 0, "pair sweep: media consistent after every pair of failures", __LINE__);
    if (bad) {
        report("      first i="); report_i(first_bad_i);
        report(" j="); report_i(first_bad_j); report(" ");
        report_media(&first_bad); report("\n");
    }
    check_at(unreported == 0, "pair sweep: a leak is never reported as success", __LINE__);
    if (unreported) {
        report("      first i="); report_i(first_unrep_i);
        report(" j="); report_i(first_unrep_j); report("\n");
    }
}

static void run_all_pair_sweeps(void)
{
    pair_sweep("ext2_create (1 ブロック)", op_p_create, 1);
    pair_sweep("ext2_create (13 ブロック = 単一間接)", op_p_create_big, 1);
    pair_sweep("ext2_write 伸長 (4B -> 13 ブロック)", op_p_write_big, 1);
    pair_sweep("ext2_write 縮小 (13 ブロック -> 4B = truncate)", op_p_truncate, 1);
    pair_sweep("ext2_unlink (1 ブロック)", op_p_unlink, 1);
    pair_sweep("ext2_unlink (13 ブロック = 単一間接)", op_p_unlink_big, 1);
    pair_sweep("ext2_rename (ファイル / 同じディレクトリ)", op_p_rename_same, 1);
    pair_sweep("ext2_rename (ファイル / 別のディレクトリへ)", op_p_rename_cross, 1);
    pair_sweep("ext2_rename (ディレクトリ / 別の親へ)", op_p_rename_dir, 1);
    x3_begin(g_p_dir, "c1", g_x3_pair_old, g_x3_pair_new);
    pair_sweep("ext2_rename (既存ファイルを置き換える)", op_p_rename_replace, 1);
    x3_end("ext2_rename (既存ファイルを置き換える / 二重故障)");
    pair_sweep("ext2_mkdir", op_p_mkdir, 1);
    pair_sweep("ext2_rmdir (空のディレクトリ)", op_p_rmdir, 1);
}

static void stage_h_pair_sweeps(void)
{
    u32 i;
    int f0 = g_failures;

    report("== 段 H: 独立した 2 か所の失敗 (i 番目と j 番目の I/O だけが落ちる) ==\n");
    report("        上限 N<="); report_i(g_pair_maxn);
    report(" (B8_PAIR_MAXN)\n");
    fault_done();
    disk_setup();
    if (g_failures != f0) { disk_teardown(); return; }

    for (i = 0; i < sizeof(g_sw_pat); i++) g_sw_pat[i] = (u8)(i * 31 + 7);

    CHECK(ext2_vfs_mkdir(g_ec, "/p") == VFS_OK);
    CHECK(ext2_vfs_mkdir(g_ec, "/q") == VFS_OK);
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/p", &g_p_dir) == EXT2_OK);
    CHECK(ext2_lookup(g_ec, "/q", &g_q_dir) == EXT2_OK);
    CHECK(ext2_create(g_ec, g_p_dir, "c1", "x", 1) == EXT2_OK);
    CHECK(ext2_create(g_ec, g_p_dir, "mv", "x", 1) == EXT2_OK);
    CHECK(ext2_find_entry(g_ec, g_p_dir, "c1", &g_x3_pair_old, (u8 *)0) == EXT2_OK);
    CHECK(ext2_find_entry(g_ec, g_p_dir, "mv", &g_x3_pair_new, (u8 *)0) == EXT2_OK);
    CHECK(ext2_vfs_write(g_ec, "/p/big13", g_sw_pat, PAIR_BIG_BYTES) == VFS_OK);
    CHECK(ext2_vfs_write(g_ec, "/p/t", g_sw_pat, PAIR_BIG_BYTES) == VFS_OK);
    CHECK(ext2_vfs_write(g_ec, "/p/w", "tiny", 4) == VFS_OK);
    CHECK(ext2_vfs_mkdir(g_ec, "/p/d") == VFS_OK);
    CHECK(ext2_vfs_mkdir(g_ec, "/p/dm") == VFS_OK);
    CHECK(ext2_sync(g_ec) == EXT2_OK);
    if (g_failures != f0) { disk_teardown(); return; }

    scribble_free_blocks();
    g_sw_pattern_name = "番号の模様";
    g_sw_pattern_idx = 0;
    run_all_pair_sweeps();

    scribble_free_blocks_dirlike(g_ec->sb_info.total_inodes);
    g_sw_pattern_name = "ディレクトリ風の模様";
    g_sw_pattern_idx = 1;
    run_all_pair_sweeps();

    disk_teardown();
}

/* ======================================================================== */
/*  段 E: 往復 3 レビューの反例 (X1 / X2 / X3 / X4) — 往復 4                  */
/* ======================================================================== */

/* レビュアーが rv_host.c (本ファイルの写し) で実行した反例を、期待値つきで
 * 本試験へ取り込んだもの。いずれも「解放済み inode を指す名前」が起きること
 * を**媒体の状態**で見る。1 件ごとに新しいディスクを作る。 */

static int g_q_hits;
static void q_cb(const Ext2DirEntry *e, void *ctx)
{
    (void)ctx;
    if (e->name_len == 1 && e->name[0] == 'q') g_q_hits++;
}

/* X1: add_entry が既存の単一間接表へ新ブロックを**中身を書く前に**繋いでいた。
 * 新ブロックの中身の書き込みが 1 回落ちると、前の持ち主のバイト列 (未使用 inode
 * を指す "q") がディレクトリとして見え、次に作ったファイルがその inode を受け
 * 取って q と別名になり、unlink(q) がそのファイルを壊した。 */
static void case_x1_add_entry_content_before_link(void)
{
    MediaReport before, after;
    Ext2Inode di, di2;
    u32 d2 = 0, phantom, tmp = 0, b1, b2;
    int rc, p, q, variant;
    u8 ftype = 0;

    for (variant = 0; variant < 2; variant++) {
        int f0 = g_failures;
        report(variant == 0
               ? "  [X1] add_entry: 新ブロックの中身の書き込みが落ちる (既存の単一間接へ足す)\n"
               : "  [X1-link] add_entry: 既存の単一間接表へ繋ぐ書き込みが落ちる\n");
        disk_setup();
        if (g_failures != f0) { disk_teardown(); return; }
        CHECK(make_full_dir("/dent2", DENT_FULL_IND, &d2, g_dent_name2));
        CHECK(ext2_read_inode(g_ec, d2, &di) == EXT2_OK);
        CHECK(di.block[EXT2_IND_BLOCK] != 0);

        /* 次に配られる inode: create が 1 つ使うので、その次を phantom にする */
        p = ext2_alloc_inode(g_ec);
        q = ext2_alloc_inode(g_ec);
        CHECK(p > 0 && q > 0);
        CHECK(ext2_free_inode(g_ec, (u32)q) == EXT2_OK);
        CHECK(ext2_free_inode(g_ec, (u32)p) == EXT2_OK);
        phantom = (u32)q;

        /* 次に配られる 2 ブロック: 1 本目はデータ、2 本目がディレクトリの新ブロック */
        b1 = (u32)ext2_alloc_block(g_ec);
        b2 = (u32)ext2_alloc_block(g_ec);
        CHECK(ext2_free_block(g_ec, b2) == EXT2_OK);
        CHECK(ext2_free_block(g_ec, b1) == EXT2_OK);
        CHECK(ext2_sync(g_ec) == EXT2_OK);
        scribble_free_blocks_dirlike(phantom);

        media_check(&before);
        CHECK_MEDIA(&before);

        if (variant == 0) wfail_arm(g_ec->base_lba + b2 * 2, 1);
        else              wfail_arm(g_ec->base_lba + di.block[EXT2_IND_BLOCK] * 2 + 1, 1);
        rc = ext2_create(g_ec, d2, g_dent_name2, "x", 1);
        wfail_disarm();
        fault_done();
        media_check(&after);

        CHECK(g_wfail_fired == 1);
        CHECK(rc == EXT2_ERR_IO);
        CHECK_MEDIA(&after);                         /* 判定の中心 */
        e2f_sample(variant == 0 ? "E X1 after fault" : "E X1-link after fault", &after);
        CHECK(ext2_find_entry(g_ec, d2, "q", &tmp, &ftype) == EXT2_ERR_NOTFOUND);
        g_q_hits = 0;
        CHECK(ext2_list_dir(g_ec, d2, q_cb, 0) == EXT2_OK);
        CHECK(g_q_hits == 0);
        CHECK(ext2_read_inode(g_ec, d2, &di2) == EXT2_OK);
        if (variant == 0) {
            /* 繋いでいない。size も伸ばす前か、伸びても末尾の穴 */
            CHECK(block_in_use(b2) == 0);            /* 中身が書けなかったので返した */
        }

        /* 追い打ち (直す前はここで victim が q と別名になった) */
        CHECK(ext2_create(g_ec, d2, "victim", "V", 1) == EXT2_OK);
        CHECK(ext2_find_entry(g_ec, d2, "victim", &tmp, &ftype) == EXT2_OK);
        CHECK(ext2_unlink(g_ec, d2, "q") == EXT2_ERR_NOTFOUND);
        CHECK(ext2_find_entry(g_ec, d2, "victim", &tmp, &ftype) == EXT2_OK);
        CHECK(raw_inode_used(tmp) == 1);
        media_check(&after);
        CHECK_MEDIA(&after);
        e2f_sample(variant == 0 ? "E X1 after follow-ups" : "E X1-link after follow-ups", &after);
        disk_teardown();
    }
}

/* X2: delete_entry の併合が消したエントリの inode 番号をスラックに残し、後の
 * 追加の部分書き込み (2 セクタ目だけ落ちる) で**消した名前が復活**した。
 * 復活した名前は、その inode 番号を再利用した別のファイル F を指し、unlink が
 * F の inode とブロックを返した (F の名前は残る)。
 *   variant 0: 往復 4 の削除 (スラックの inode 番号は 0) / 2 セクタ目が落ちる
 *   variant 1: 同上 / 見せる書き込み (rec_len、1 セクタ目) が落ちる
 *   variant 2: **旧コードの削除の跡** (スラックに inode 番号が残る) / 2 セクタ目
 *              — 既存の NHD に残っている状態。0 書きに頼らず、追加の順序だけで
 *              防げていることを見る */
static void case_x2_slack_resurrection(void)
{
    static char nC[ST_NAME_LEN + 1];
    MediaReport before, after;
    Ext2Inode di;
    u32 st = 0, c_ino = 0, f_ino = 0, tmp = 0, lba;
    int rc, variant;
    u8 ftype = 0;

    st_name(nC, 'C');
    for (variant = 0; variant < 3; variant++) {
        int f0 = g_failures;
        report(variant == 0 ? "  [X2] スラックに消した跡 / 追加の 2 セクタ目だけ落ちる\n" :
               variant == 1 ? "  [X2-commit] スラックに消した跡 / 見せる書き込みが落ちる\n" :
                              "  [X2-legacy] 旧コードの跡 (inode 番号が残る) / 2 セクタ目だけ落ちる\n");
        disk_setup();
        if (g_failures != f0) { disk_teardown(); return; }
        if (!make_slack_dir("/st", "/etc/F", variant == 2, &st, &c_ino)) {
            report("  (harness) slack dir setup failed\n");
            g_failures++;
            disk_teardown();
            return;
        }
        memo_cold();
        CHECK(ext2_lookup(g_ec, "/etc/F", &f_ino) == EXT2_OK);
        CHECK(f_ino == c_ino);
        CHECK(ext2_read_inode(g_ec, st, &di) == EXT2_OK);
        lba = g_ec->base_lba + di.block[0] * 2;
        CHECK(*(const u32 *)(raw_blk(di.block[0]) + 528) == (variant == 2 ? c_ino : 0));

        media_check(&before);
        CHECK_MEDIA(&before);
        if (variant == 1) wfail_arm(lba, 2);         /* 1 回目 = 中身、2 回目 = rec_len */
        else              wfail_arm(lba + 1, 1);
        rc = ext2_create(g_ec, st, g_st_name_d, "", 0);
        wfail_disarm();
        fault_done();
        media_check(&after);

        CHECK(g_wfail_fired == 1);
        CHECK(rc == EXT2_ERR_IO);
        CHECK_MEDIA(&after);                         /* 判定の中心 */
        e2f_sample(variant == 0 ? "E X2 after fault" : variant == 1 ? "E X2-commit after fault"
                                                                      : "E X2-legacy after fault", &after);
        CHECK(ext2_find_entry(g_ec, st, nC, &tmp, &ftype) == EXT2_ERR_NOTFOUND);

        /* 追い打ち (直す前はここで F の inode が返された) */
        CHECK(ext2_unlink(g_ec, st, nC) == EXT2_ERR_NOTFOUND);
        CHECK(raw_inode_used(f_ino) == 1);
        memo_cold();
        CHECK(ext2_lookup(g_ec, "/etc/F", &tmp) == EXT2_OK);
        /* やり直せば足せる */
        CHECK(ext2_create(g_ec, st, g_st_name_d, "", 0) == EXT2_OK);
        media_check(&after);
        CHECK_MEDIA(&after);
        e2f_sample(variant == 0 ? "E X2 after follow-ups" : variant == 1 ? "E X2-commit after follow-ups"
                                                                           : "E X2-legacy after follow-ups", &after);
        disk_teardown();
    }
}

/* X3: rename の途中から装置が消える (sticky) と、2 つの名前が links 1 の inode を
 * 指したまま失敗を返した。復旧後に片方を unlink すると、もう片方が解放済み inode
 * を指した (レビュー実測: 35 位置中 10 位置、10/10 で dangling)。
 * 往復 4 は遷移中 links_count を上げるので、2 つの名前が残っても links は 2。 */
static void case_x3_rename_two_names(void)
{
    MediaReport r;
    u32 sw = 0, i_old, i_new;
    int at, rc, fired, both = 0, bad = 0, bad_after_unlink = 0;
    int f0 = g_failures;
    u8 t;

    report("  [X3] rename sticky -> 2 つの名前 -> 復旧後に片方を unlink\n");
    fault_done();   /* 前の case のエラー状態を持ち越さない (票 B8 往復 5) */
    disk_setup();
    if (g_failures != f0) { disk_teardown(); return; }
    CHECK(ext2_vfs_mkdir(g_ec, "/sw") == VFS_OK);
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/sw", &sw) == EXT2_OK);
    CHECK(ext2_vfs_write(g_ec, "/sw/big", "hello", 5) == VFS_OK);

    for (at = 1; at < SWEEP_MAX; at++) {
        undo_begin();
        remount_cold();
        sw_arm(at, 1, SW_KIND_ANY);
        rc = ext2_rename(g_ec, sw, "big", sw, "moved");
        (void)rc;
        fired = g_sw_fired;
        sw_disarm();
        media_check(&r);
        if (!media_ok(&r)) bad++;
        if (e2f_pick(at)) {
            lbl_reset(); lbl_s("E X3 rename sticky at="); lbl_i(at);
            e2f_sample(g_lbl, &r);
        }
        i_old = 0; i_new = 0;
        remount_cold();
        if (ext2_find_entry(g_ec, sw, "big", &i_old, &t) == EXT2_OK &&
            ext2_find_entry(g_ec, sw, "moved", &i_new, &t) == EXT2_OK && i_old == i_new) {
            both++;
            CHECK(ext2_unlink(g_ec, sw, "big") == EXT2_OK);
            media_check(&r);
            if (!media_ok(&r)) bad_after_unlink++;
            if (both <= 3) {
                lbl_reset(); lbl_s("E X3 two names then unlink at="); lbl_i(at);
                e2f_sample(g_lbl, &r);
            }
            CHECK(raw_inode_used(i_new) == 1);       /* moved はまだ生きている */
        }
        undo_rollback();
        if (!fired) break;
    }
    report("      at-runs="); report_i(at); report(" two-names-runs="); report_i(both);
    report(" inconsistent="); report_i(bad);
    report(" inconsistent-after-unlink="); report_i(bad_after_unlink); report("\n");
    CHECK(bad == 0);
    CHECK(bad_after_unlink == 0);
    remount_cold();
    disk_teardown();
}

/* X4: mkdir で add_entry が I/O で落ちる。名前が載ったか区別できないので
 * 何も触らない (孤児で残す) — 媒体は整合し、親の links は名前の数以上。
 * NOSPC のときに返すのは段 D の [X4'] で見る。 */
static void case_x4_mkdir_add_entry_io(void)
{
    MediaReport before, after;
    Ext2Inode ri;
    u32 root = 0, tmp = 0;
    int rc, f0 = g_failures;
    u8 t = 0;

    report("  [X4] mkdir: add_entry が I/O で落ちる -> 何も触らず孤児で残す\n");
    fault_done();   /* 前の case のエラー状態を持ち越さない (票 B8 往復 5) */
    disk_setup();
    if (g_failures != f0) { disk_teardown(); return; }
    CHECK(ext2_lookup(g_ec, "/", &root) == EXT2_OK);
    CHECK(ext2_read_inode(g_ec, root, &ri) == EXT2_OK);
    media_check(&before);
    fail_arm(g_ec->base_lba + ri.block[0] * 2, 2);   /* 1 回目 = 存在確認、2 回目 = add_entry */
    rc = ext2_mkdir(g_ec, root, "nd");
    fail_disarm();
    fault_done();
    media_check(&after);
    CHECK(g_fail_fired == 1);
    CHECK(rc == EXT2_ERR_IO);
    CHECK_MEDIA(&after);
    CHECK(leak_delta(&after, &before) == 0);          /* 孤児から辿れる */
    CHECK(after.links_surplus > before.links_surplus); /* 孤児 (名前 < links) */
    e2f_sample("E X4 mkdir add_entry io", &after);
    CHECK(ext2_find_entry(g_ec, root, "nd", &tmp, &t) == EXT2_ERR_NOTFOUND);
    disk_teardown();
}

static void stage_e_review_cases(void)
{
    report("== 段 E: 往復 3 レビューの反例 ==\n");
    case_x1_add_entry_content_before_link();
    case_x2_slack_resurrection();
    case_x3_rename_two_names();
    case_x4_mkdir_add_entry_io();
}

/* ======================================================================== */
/*  段 D: 空きを使い切った状態 (create の add_entry が NOSPC)                 */
/* ======================================================================== */

/* inode は媒体に書いた後なので、**参照を先に外してから** inode とブロックを返す。 */
static void case_create_add_entry_nospc(void)
{
    MediaReport before, after;
    u32 dent = 0, tmp = 0, fi_before, bm_lba, guess;
    int rc, blk, last = -1, prev = -1;
    int f0 = g_failures;
    u8 ftype;

    report("== 段 D: 空きを使い切った状態 ==\n");
    report("  [P1-C'] create: add_entry が NOSPC -> 参照を外してから返す\n");
    fault_done();   /* 前の case のエラー状態を持ち越さない (票 B8 往復 5) */
    disk_setup();
    if (g_failures != f0) { disk_teardown(); return; }

    CHECK(make_full_dir("/dent", DENT_FULL, &dent, g_dent_name));
    bm_lba = g_ec->base_lba + g_ec->gd_table[0].block_bitmap * 2;

    /* 空きを 1 ブロックだけ残す (割り当てたまま誰も指さない = 足場の漏れ) */
    while (g_ec->sb_info.free_blocks_count > 1) {
        blk = ext2_alloc_block(g_ec);
        if (blk < 0) break;
        prev = last;
        last = blk;
    }
    CHECK(g_ec->sb_info.free_blocks_count == 1);
    CHECK(last > 0);
    CHECK(ext2_sync(g_ec) == EXT2_OK);

    /* (1) 素直に落ちる: 1 ブロックと inode を返せる */
    fi_before = g_ec->sb_info.free_inodes_count;
    media_check(&before);
    rc = ext2_create(g_ec, dent, g_dent_name, "Z", 1);
    media_check(&after);
    CHECK(rc == EXT2_ERR_NOSPC);
    CHECK_MEDIA(&after);
    CHECK(leak_delta(&after, &before) == 0);
    CHECK(g_ec->sb_info.free_blocks_count == 1);
    CHECK(g_ec->sb_info.free_inodes_count == fi_before);
    CHECK(ext2_find_entry(g_ec, dent, g_dent_name, &tmp, &ftype) == EXT2_ERR_NOTFOUND);

    /* (2) 後始末の解放が落ちる: ブロックビットマップの 2 回目の書き込み
     *     (1 回目は create の割り当て)。ブロックだけ漏れ、inode は返す */
    media_check(&before);
    wfail_arm(bm_lba, 2);
    rc = ext2_create(g_ec, dent, g_dent_name, "Z", 1);
    wfail_disarm();
    fault_done();
    media_check(&after);
    CHECK(g_wfail_fired == 1);
    CHECK(rc == EXT2_ERR_NOSPC);
    CHECK_MEDIA(&after);
    CHECK(leak_delta(&after, &before) == 1);
    CHECK(g_ec->sb_info.free_inodes_count == fi_before);

    /* (3) 参照を外す書き込み (inode の 2 回目の書き込み) が落ちる:
     *     **何も返さない** — inode もブロックも孤児として残す */
    CHECK(ext2_free_block(g_ec, (u32)last) == EXT2_OK);   /* 空きを 1 に戻す */
    {
        int probe = ext2_alloc_inode(g_ec);           /* 次に配られる inode 番号 */
        CHECK(probe > 0);
        guess = (u32)probe;
        CHECK(ext2_free_inode(g_ec, guess) == EXT2_OK);
    }
    media_check(&before);
    wfail_arm(lba_of_inode(guess), 2);
    rc = ext2_create(g_ec, dent, g_dent_name, "Z", 1);
    wfail_disarm();
    fault_done();
    media_check(&after);
    CHECK(g_wfail_fired == 1);
    CHECK(rc == EXT2_ERR_NOSPC);
    CHECK_MEDIA(&after);
    CHECK(raw_inode_used(guess) == 1);
    CHECK(raw_inode_ptr(guess, 0) != 0);
    CHECK(block_in_use(raw_inode_ptr(guess, 0)) == 1);
    CHECK(leak_delta(&after, &before) == 0);          /* 孤児から辿れる */

    /* (4) mkdir の add_entry が NOSPC (レビュー非 blocker: create と揃えた)。
     *     参照を外して inode とブロックを返し、親の links_count も戻す */
    report("  [X4'] mkdir: add_entry が NOSPC -> 参照を外して返し、親の links も戻す\n");
    CHECK(prev > 0);
    CHECK(ext2_free_block(g_ec, (u32)prev) == EXT2_OK);   /* 空き 1 = mkdir 自身の分 */
    CHECK(ext2_sync(g_ec) == EXT2_OK);
    fi_before = g_ec->sb_info.free_inodes_count;
    {
        u16 links_before = *(const u16 *)(raw_inode(dent) + 26);
        media_check(&before);
        rc = ext2_mkdir(g_ec, dent, g_dent_name);
        media_check(&after);
        CHECK(rc == EXT2_ERR_NOSPC);
        CHECK_MEDIA(&after);
        CHECK(leak_delta(&after, &before) == 0);
        CHECK(g_ec->sb_info.free_inodes_count == fi_before);
        CHECK(*(const u16 *)(raw_inode(dent) + 26) == links_before);
        CHECK(ext2_find_entry(g_ec, dent, g_dent_name, &tmp, &ftype) == EXT2_ERR_NOTFOUND);
    }

    disk_teardown();
}

/* 書き込み系の最後の ext2_sync の失敗を捨てない (P1-5 と同じ形の洗い出し)。
 *
 * ext2_sync は空き数・グループ記述子を書き戻す。この FS の約束は
 * **「戻った時点でディスクが正しい」(write-through)** なので、書き戻せ
 * なかったのに成功と言うのはその約束の嘘になる。
 *
 * ext2_write_super_raw はスーパーブロック (ブロック 1) を read-modify-write
 * するので、その**読み出し**を落とせば sync を失敗させられる。 */
static void case_trailing_sync_failure(void)
{
    u32 sb_lba, etc_ino = 0, tmp = 0;
    int rc;

    report("  [SYNC] create / write / mkdir / rename / 追記 が sync 失敗を返す\n");
    fault_done();   /* 前の case のエラー状態を持ち越さない (票 B8 往復 5) */

    sb_lba = g_ec->base_lba + 1 * 2;
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/etc", &etc_ino) == EXT2_OK);

    /* create (新しい inode とブロックを割り当てる = 書き戻すものがある) */
    fail_arm_always(sb_lba);
    rc = ext2_create(g_ec, etc_ino, "sync1", "abc", 3);
    fail_disarm();
    fault_done();
    CHECK(g_fail_fired > 0);
    CHECK(rc == EXT2_ERR_IO);

    /* write (切り詰めて書き直す = 解放と割り当て) */
    fail_arm_always(sb_lba);
    rc = ext2_vfs_write(g_ec, "/etc/sync1", "defgh", 5);
    fail_disarm();
    fault_done();
    CHECK(g_fail_fired > 0);
    CHECK(rc == VFS_ERR_IO);

    /* mkdir */
    fail_arm_always(sb_lba);
    rc = ext2_mkdir(g_ec, etc_ino, "syncdir");
    fail_disarm();
    fault_done();
    CHECK(g_fail_fired > 0);
    CHECK(rc == EXT2_ERR_IO);

    /* 追記でブロックを新しく割り当てる (P1-5 の sync 側) */
    {
        static u8 blk[1024];
        kmemset(blk, 'Z', sizeof(blk));
        fail_arm_always(sb_lba);
        rc = ext2_vfs_write_stream(g_ec, "/etc/sync1", blk, sizeof(blk), 5);
        fail_disarm();
        fault_done();
        CHECK(g_fail_fired > 0);
        CHECK(rc == VFS_ERR_IO);
    }

    /* rename はそれ自体では空き数を動かさないので、書き戻し待ちが
     * 残っている状態 (前の操作の書き戻しが失敗した直後と同じ) を作る */
    ext2_meta_touch(g_ec);
    fail_arm_always(sb_lba);
    rc = ext2_rename(g_ec, etc_ino, "sync1", etc_ino, "sync2");
    fail_disarm();
    fault_done();
    CHECK(g_fail_fired > 0);
    CHECK(rc == EXT2_ERR_IO);

    /* 回帰: 読めるなら全部通る */
    CHECK(ext2_sync(g_ec) == EXT2_OK);
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/etc/sync2", &tmp) == EXT2_OK);
    CHECK(ext2_create(g_ec, etc_ino, "sync3", "x", 1) == EXT2_OK);
    CHECK(ext2_mkdir(g_ec, etc_ino, "syncdir2") == EXT2_OK);
    CHECK(ext2_rename(g_ec, etc_ino, "sync3", etc_ino, "sync4") == EXT2_OK);
}

/* 正常系の回帰 — 直した結果ふつうの使い方が壊れていないこと */
static void case_normal_paths(void)
{
    int fd;
    u32 sz = 0;

    report("  [A4] 正常系の回帰\n");

    memo_cold();
    fd = vfs_open("/big/keepme", O_RDONLY);
    CHECK(fd >= 3);                                    /* 通常ファイルは開ける */
    if (fd >= 0) vfs_close(fd);
    CHECK(keep_intact());

    CHECK(vfs_open("/etc", O_RDONLY) == VFS_ERR_ISDIR);        /* ディレクトリ */
    CHECK(vfs_open("/big", O_WRONLY | O_CREAT) == VFS_ERR_ISDIR);

    /* 本当に不存在なら O_CREAT で作れる */
    CHECK(vfs_open("/etc/brandnew", O_RDONLY) == VFS_ERR_NOTFOUND);
    fd = vfs_open("/etc/brandnew", O_WRONLY | O_CREAT);
    CHECK(fd >= 3);
    if (fd >= 0) vfs_close(fd);
    CHECK(ext2_vfs_get_size(g_ec, "/etc/brandnew", &sz) == VFS_OK);
    CHECK(sz == 0);

    /* O_TRUNC は本当に切り詰める */
    CHECK(ext2_vfs_write(g_ec, "/etc/trunc", "0123456789", 10) == VFS_OK);
    CHECK(ext2_vfs_get_size(g_ec, "/etc/trunc", &sz) == VFS_OK);
    CHECK(sz == 10);
    fd = vfs_open("/etc/trunc", O_WRONLY | O_TRUNC);
    CHECK(fd >= 3);
    if (fd >= 0) vfs_close(fd);
    CHECK(ext2_vfs_get_size(g_ec, "/etc/trunc", &sz) == VFS_OK);
    CHECK(sz == 0);

    /* O_CREAT ありでも、既存ファイルは中身を保ったまま開ける */
    CHECK(keep_intact());
    fd = vfs_open("/big/keepme", O_WRONLY | O_CREAT);
    CHECK(fd >= 3);
    if (fd >= 0) vfs_close(fd);
    CHECK(keep_intact());

    /* ディレクトリはサイズ取得にも答えない (③) */
    CHECK(ext2_vfs_get_size(g_ec, "/etc", &sz) == VFS_ERR_ISDIR);
    CHECK(vfs_path_kind("/etc") == VFS_KIND_DIR);
}

/* ext2_bmap の区別そのもの — 未割当は EXT2_OK + 0、読めなければ EXT2_ERR_IO */
static void case_bmap_contract(void)
{
    Ext2Inode dir;
    u32 ino = 0, phys = 12345;
    u32 lba;

    report("  [A5] ext2_bmap の約束 (未割当と I/O エラーを分ける)\n");

    memo_cold();
    CHECK(ext2_lookup(g_ec, "/big", &ino) == EXT2_OK);
    CHECK(ext2_read_inode(g_ec, ino, &dir) == EXT2_OK);

    /* 未割当: ずっと先の論理ブロックは EXT2_OK で 0 */
    phys = 12345;
    CHECK(ext2_bmap(g_ec, &dir, 60000, &phys) == EXT2_OK);
    CHECK(phys == 0);

    /* 割当済み: 直接ブロック 0 は非 0 */
    phys = 0;
    CHECK(ext2_bmap(g_ec, &dir, 0, &phys) == EXT2_OK);
    CHECK(phys != 0);

    /* 間接ブロックが読めない: EXT2_ERR_IO で、0 (未割当) と区別できる */
    lba = g_ec->base_lba + dir.block[EXT2_IND_BLOCK] * 2;
    phys = 12345;
    fail_arm(lba, 1);
    CHECK(ext2_bmap(g_ec, &dir, EXT2_NDIR_BLOCKS, &phys) == EXT2_ERR_IO);
    fail_disarm();
    CHECK(g_fail_fired == 1);

    /* 同じ経路が find_entry / lookup / stat / path_kind まで畳まれずに届く */
    {
        OS32_Stat st;
        u32 out = 0;
        Ext2Inode big;
        CHECK(ext2_read_inode(g_ec, ino, &big) == EXT2_OK);

        memo_cold();
        fail_arm(lba, 1);
        CHECK(ext2_find_entry(g_ec, ino, "keepme", &out, (u8 *)0) == EXT2_ERR_IO);
        fail_disarm();

        memo_cold();
        fail_arm(lba, 1);
        CHECK(ext2_lookup(g_ec, "/big/keepme", &out) == EXT2_ERR_IO);
        fail_disarm();

        memo_cold();
        fail_arm(lba, 1);
        CHECK(ext2_vfs_stat(g_ec, "/big/keepme", &st) == VFS_ERR_IO);
        fail_disarm();

        memo_cold();
        fail_arm(lba, 1);
        CHECK(vfs_path_kind("/big/keepme") == VFS_ERR_IO);
        fail_disarm();

        /* ext2_vfs_get_size も NOTFOUND に畳まない */
        memo_cold();
        fail_arm(lba, 1);
        CHECK(ext2_vfs_get_size(g_ec, "/big/keepme", &out) == VFS_ERR_IO);
        fail_disarm();
    }
}

/* ======================================================================== */
/*  段 F: 往復 5 — ディレクトリ rename の順序 (決裁 1) とエラー状態 (決裁 2)    */
/*                                                                          */
/*  **試験の組み方** (PM 指示): エラー状態は**そのセッションの間だけ**書き込み  */
/*  を止める。再起動後は読み書きでマウントされるので、1 回目の失敗で媒体に    */
/*  残った状態は、再マウント後のあらゆる後続操作に対して安全でなければならない */
/*  そこで「失敗を注入 -> 以後の書き込みが断られることを確認 -> **新しい ctx で */
/*  同じ RAM ディスクを再マウント** (remount_fresh) -> 後続操作 -> 媒体検査と   */
/*  e2fsck」の形にする。エラー状態が後続操作を止めることで blocker が試験から  */
/*  見えなくなる、ということが無いように。                                   */
/* ======================================================================== */

static u16 sb_state_raw(void)
{
    return *(const u16 *)(g_disk + SB_STATE_LBA * 512u + EXT2_SB_STATE_OFF);
}
static u16 sb_errors_raw(void)
{
    return *(const u16 *)(g_disk + SB_STATE_LBA * 512u + EXT2_SB_ERRORS_OFF);
}

static int g_ls_n;
static void ls_count_cb(const VfsDirEntry *e, void *ctx) { (void)e; (void)ctx; g_ls_n++; }

/* [R5-ES] エラー状態の振る舞いそのもの */
static void case_r5_error_state(void)
{
    MediaReport r;
    Ext2Inode fi, ei;
    u32 etc = 0, plain = 0, tmp = 0;
    static u8 buf[64];
    int rc, fd, f0 = g_failures;

    report("  [R5-ES] エラー状態: メタデータの I/O エラーで以後の書き込みを断る\n");
    fault_done();
    disk_setup();
    if (g_failures != f0) { disk_teardown(); return; }

    /* フォーマットが書く値 */
    CHECK((sb_state_raw() & EXT2_VALID_FS) != 0);
    CHECK((sb_state_raw() & EXT2_ERROR_FS) == 0);
    CHECK(sb_errors_raw() == EXT2_ERRORS_RO);
    CHECK(g_ec->fs_error == 0);
    CHECK(g_ec->mounted_with_errors == 0);
    CHECK(VFS_ERR_ROFS == -15);

    memo_cold();
    CHECK(ext2_lookup(g_ec, "/etc", &etc) == EXT2_OK);
    CHECK(ext2_lookup(g_ec, "/etc/plain", &plain) == EXT2_OK);
    CHECK(ext2_read_inode(g_ec, plain, &fi) == EXT2_OK);
    CHECK(ext2_read_inode(g_ec, etc, &ei) == EXT2_OK);

    /* (a) データブロックの読み取り失敗はエラー状態にしない (Linux と同じ) */
    fail_arm(g_ec->base_lba + fi.block[0] * 2, 1);
    rc = ext2_read_file(g_ec, plain, buf, sizeof(buf));
    fail_disarm();
    CHECK(g_fail_fired == 1);
    CHECK(rc == EXT2_ERR_IO);
    CHECK(g_ec->fs_error == 0);
    CHECK((sb_state_raw() & EXT2_ERROR_FS) == 0);

    /* (b) データブロックの書き込み失敗もしない */
    wfail_arm(g_ec->base_lba + fi.block[0] * 2, 1);
    rc = ext2_vfs_write_stream(g_ec, "/etc/plain", "Q", 1, 0);
    wfail_disarm();
    CHECK(g_wfail_fired == 1);
    CHECK(rc < 0);
    CHECK(g_ec->fs_error == 0);
    CHECK(vfs_mkdir("/okdir") == VFS_OK);          /* 書き込みは通る */

    /* (c) メタデータ (ディレクトリブロック) の読み取り失敗 -> エラー状態 */
    g_kp_fs_error = 0;
    memo_cold();
    io_reset();
    fail_arm(g_ec->base_lba + ei.block[0] * 2, 1);
    rc = vfs_path_kind("/etc/plain");
    fail_disarm();
    CHECK(g_fail_fired == 1);
    CHECK(rc == VFS_ERR_IO);
    CHECK(g_ec->fs_error == 1);
    CHECK(g_kp_fs_error == 1);                      /* 1 度だけ知らせる */
    CHECK((sb_state_raw() & EXT2_ERROR_FS) != 0);   /* 媒体に印 */
    CHECK(g_wr_sb == 1);                            /* 印を書いた 1 セクタだけ */
    CHECK(wr_non_state() == 0);

    /* 読み取り系は通る */
    memo_cold();
    CHECK(vfs_path_kind("/etc/plain") == VFS_KIND_FILE);
    CHECK(ext2_read_file(g_ec, plain, buf, sizeof(buf)) == 5);
    g_ls_n = 0;
    CHECK(vfs_ls("/etc", ls_count_cb, (void *)0) == VFS_OK);
    CHECK(g_ls_n > 0);
    fd = vfs_open("/etc/plain", O_RDONLY);
    CHECK(fd >= 3);
    if (fd >= 0) vfs_close(fd);

    /* 書き込み系はすべて ROFS で、1 セクタも書かない */
    io_reset();
    CHECK(vfs_write("/etc/plain", "x", 1) == VFS_ERR_ROFS);
    CHECK(ext2_vfs_write_stream(g_ec, "/etc/plain", "x", 1, 0) == VFS_ERR_ROFS);
    CHECK(ext2_create(g_ec, etc, "newf", "x", 1) == EXT2_ERR_ROFS);
    CHECK(vfs_mkdir("/etc/nd") == VFS_ERR_ROFS);
    CHECK(vfs_rmdir("/okdir") == VFS_ERR_ROFS);
    CHECK(vfs_rm("/etc/plain") == VFS_ERR_ROFS);
    CHECK(vfs_rename("/etc/plain", "/etc/p2") == VFS_ERR_ROFS);
    CHECK(vfs_set_mtime("/etc/plain", (os_time_t)12345) == VFS_ERR_ROFS);
    CHECK(vfs_open("/etc/brandnew", O_WRONLY | O_CREAT) == VFS_ERR_ROFS);
    CHECK(vfs_open("/etc/plain", O_WRONLY | O_TRUNC) == VFS_ERR_ROFS);
    CHECK(any_fd_open() == 0);
    CHECK(g_wr_sect == 0);
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/etc/plain", &tmp) == EXT2_OK);
    CHECK(ext2_lookup(g_ec, "/okdir", &tmp) == EXT2_OK);
    media_check(&r);
    CHECK_MEDIA(&r);
    e2f_sample("F R5-ES error state set", &r);

    /* (d) 再マウント (新しい ctx): 警告を出して**読み書きで**マウントする */
    g_kp_mount_warn = 0;
    remount_fresh();
    if (!g_ec) return;
    CHECK(g_kp_mount_warn == 1);
    CHECK(g_ec->mounted_with_errors == 1);
    CHECK(g_ec->fs_error == 0);
    CHECK(vfs_mkdir("/etc/after") == VFS_OK);
    CHECK((sb_state_raw() & EXT2_ERROR_FS) != 0);   /* 印は e2fsck だけが消す */
    media_check(&r);
    CHECK_MEDIA(&r);
    e2f_sample("F R5-ES remounted rw with errors flag", &r);

    /* (e) メタデータ (inode 表) の書き込み失敗でもエラー状態 */
    memo_cold();
    wfail_arm(lba_of_inode(plain), 1);
    rc = vfs_set_mtime("/etc/plain", (os_time_t)777);
    wfail_disarm();
    CHECK(g_wfail_fired == 1);
    CHECK(rc == VFS_ERR_IO);
    CHECK(g_ec->fs_error == 1);
    CHECK(vfs_mkdir("/etc/more") == VFS_ERR_ROFS);
    /* エラー状態の sync は書かない */
    io_reset();
    (void)ext2_sync(g_ec);
    CHECK(g_wr_sect == 0);
    disk_teardown();
}

/* [R5-LINK] links_count の上限 (u16 の回り込みを作らない) */
static void case_r5_link_max(void)
{
    Ext2Inode ei;
    u32 etc = 0, tmp = 0;
    u16 saved;
    int f0 = g_failures;

    report("  [R5-LINK] 親の links_count が EXT2_LINK_MAX なら mkdir / rename を断る\n");
    fault_done();
    disk_setup();
    if (g_failures != f0) { disk_teardown(); return; }
    CHECK(vfs_mkdir("/sw") == VFS_OK);
    CHECK(vfs_mkdir("/sw/d") == VFS_OK);
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/etc", &etc) == EXT2_OK);
    CHECK(ext2_read_inode(g_ec, etc, &ei) == EXT2_OK);
    saved = ei.links_count;
    ei.links_count = EXT2_LINK_MAX;
    CHECK(ext2_write_inode(g_ec, etc, &ei) == EXT2_OK);

    io_reset();
    CHECK(vfs_mkdir("/etc/toomany") == VFS_ERR_FULL);
    CHECK(vfs_rename("/sw/d", "/etc/d") == VFS_ERR_FULL);
    CHECK(g_wr_sect == 0);
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/sw/d", &tmp) == EXT2_OK);
    CHECK(ext2_lookup(g_ec, "/etc/d", &tmp) == EXT2_ERR_NOTFOUND);

    ei.links_count = saved;
    CHECK(ext2_write_inode(g_ec, etc, &ei) == EXT2_OK);
    CHECK(vfs_rename("/sw/d", "/etc/d") == VFS_OK);
    disk_teardown();
}

/* [R5-1] ディレクトリ rename を全位置で落とし、**新しい ctx で再マウントしてから**
 * 後続操作 (輪を作る rename / rmdir / mkdir / create / unlink) を実行する。
 * レビューの R5-1 (2 名 -> rmdir で dangling) を、決裁 1 の順序で塞いだことを
 * 再マウント後の操作込みで見る。 */
static void case_r5_dir_rename_followups(int cross)
{
    MediaReport r, r2;
    u32 sw = 0, etc = 0, root = 0, d = 0, dst, x = 0;
    int sticky, at, rc, fired, f0 = g_failures;
    int runs = 0, two_names = 0, orphan = 0, done_new = 0, kept_old = 0;
    int bad = 0, bad_after = 0, loop_made = 0, no_err_state = 0, not_refused = 0;
    int refused_after_remount = 0, first_bad_at = 0;
    GuardTally gt;
    u8 t;

    kmemset(&gt, 0, sizeof(gt));
    report(cross ? "  [R5-1] ディレクトリを別の親へ rename -> 全位置で落とす -> 再マウント -> 後続操作\n"
                 : "  [R5-1s] ディレクトリを同じ親の中で rename -> 全位置で落とす -> 再マウント -> 後続操作\n");
    fault_done();
    disk_setup();
    if (g_failures != f0) { disk_teardown(); return; }
    CHECK(vfs_mkdir("/sw") == VFS_OK);
    CHECK(vfs_mkdir("/sw/rmd") == VFS_OK);
    CHECK(vfs_mkdir("/sw/rmd/kid") == VFS_OK);     /* D を指す ".." を持つ子 */
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/", &root) == EXT2_OK);
    CHECK(ext2_lookup(g_ec, "/sw", &sw) == EXT2_OK);
    CHECK(ext2_lookup(g_ec, "/etc", &etc) == EXT2_OK);
    CHECK(ext2_lookup(g_ec, "/sw/rmd", &d) == EXT2_OK);
    CHECK(ext2_sync(g_ec) == EXT2_OK);
    dst = cross ? etc : sw;
    if (g_failures != f0) { disk_teardown(); return; }

    for (sticky = 0; sticky < 2; sticky++) {
        for (at = 1; at < SWEEP_MAX; at++) {
            int has_old, has_new, rc2;
            undo_begin();
            remount_cold();
            sw_arm(at, sticky, SW_KIND_ANY);
            rc = ext2_rename(g_ec, sw, "rmd", dst, "rmd2");
            fired = g_sw_fired;
            sw_disarm();
            (void)rc;

            /* このセッション: rename の I/O はすべてメタデータなので、落ちたら
             * エラー状態になり、以後の書き込みは断られる */
            if (fired) {
                if (!g_ec->fs_error) no_err_state++;
                else if (ext2_mkdir(g_ec, sw, "probe") != EXT2_ERR_ROFS) not_refused++;
            }

            /* 電源断 -> 新しい ctx (エラー状態は消え、媒体だけが残る) */
            remount_fresh();
            if (!g_ec) return;
            media_check(&r);
            if (!media_ok(&r)) { bad++; if (!first_bad_at) first_bad_at = at; }
            x = 0;
            has_old = (ext2_find_entry(g_ec, sw, "rmd", &x, &t) == EXT2_OK && x == d);
            x = 0;
            has_new = (ext2_find_entry(g_ec, dst, "rmd2", &x, &t) == EXT2_OK && x == d);
            if (has_old && has_new) two_names++;
            else if (!has_old && !has_new) orphan++;
            else if (has_new) done_new++;
            else kept_old++;
            if (e2f_pick(at) || (!has_old && !has_new && orphan == 1)) {
                lbl_reset(); lbl_s(cross ? "F R5-1 cross" : "F R5-1s same");
                lbl_s(" after fault at="); lbl_i(at); lbl_s(sticky ? " sticky" : " once");
                e2f_sample(g_lbl, &r);
            }

            /* ---- 後続操作 (新しい ctx = エラー状態ではない) ---- */
            /* (1) 輪を作ろうとする: D の祖先を D の配下へ */
            if (has_new) {
                rc2 = ext2_rename(g_ec, root, cross ? "etc" : "sw", d, "anc");
                if (rc2 == EXT2_OK) loop_made++;
                if (rc2 == EXT2_ERR_ROFS) refused_after_remount++;
            } else if (has_old) {
                rc2 = ext2_rename(g_ec, root, "sw", d, "anc");
                if (rc2 == EXT2_OK) loop_made++;
                if (rc2 == EXT2_ERR_ROFS) refused_after_remount++;
            }
            /* (2) 子と D を消す (D を指す名前すべてについて) */
            rc2 = ext2_rmdir(g_ec, d, "kid");
            if (rc2 == EXT2_ERR_ROFS) refused_after_remount++;
            if (has_old) {
                rc2 = ext2_rmdir(g_ec, sw, "rmd");
                if (rc2 == EXT2_ERR_ROFS) refused_after_remount++;
            }
            if (has_new) {
                rc2 = ext2_rmdir(g_ec, dst, "rmd2");
                if (rc2 == EXT2_ERR_ROFS) refused_after_remount++;
            }
            /* (3) mkdir (返した inode 番号を受け取り得る) と、同じ名前での作り直し */
            rc2 = ext2_mkdir(g_ec, dst, "fresh");
            if (rc2 == EXT2_ERR_ROFS) refused_after_remount++;
            rc2 = ext2_mkdir(g_ec, dst, "rmd2");
            if (rc2 == EXT2_ERR_ROFS) refused_after_remount++;
            /* (4) create / unlink */
            rc2 = ext2_create(g_ec, sw, "f", "x", 1);
            if (rc2 == EXT2_ERR_ROFS) refused_after_remount++;
            rc2 = ext2_unlink(g_ec, sw, "f");
            if (rc2 == EXT2_ERR_ROFS) refused_after_remount++;
            /* (5) 往復 6 (レビュアーの反例、ユーザー決裁): 後続で新しい親に作った
             * ものを片付けてから**旧親を rmdir -> 同じ名前で mkdir**。孤児 D の ".."
             * がまだ旧親を指していれば、旧親を返してその番号を mkdir が受け取った
             * 時点で ".." が無関係な生きたディレクトリを指す (links 不足)。
             * 別の親: 旧親 /sw は (2)(4) で空になっている。同じ親: 片付けで空になる。 */
            (void)ext2_rmdir(g_ec, dst, "fresh");
            (void)ext2_rmdir(g_ec, dst, "rmd2");
            followup_rmdir_mkdir(&gt, root, "sw");

            media_check(&r2);
            if (!media_ok(&r2)) { bad_after++; if (!first_bad_at) first_bad_at = at; }
            if (e2f_pick(at)) {
                lbl_reset(); lbl_s(cross ? "F R5-1 cross" : "F R5-1s same");
                lbl_s(" after follow-ups at="); lbl_i(at); lbl_s(sticky ? " sticky" : " once");
                e2f_sample(g_lbl, &r2);
            }
            undo_rollback();
            runs++;
            if (!fired) break;
        }
    }
    report("      runs="); report_i(runs);
    report(" two-names="); report_i(two_names);
    report(" orphan="); report_i(orphan);
    report(" moved="); report_i(done_new);
    report(" unmoved="); report_i(kept_old);
    report(" inconsistent-after-fault="); report_i(bad);
    report(" inconsistent-after-follow-ups="); report_i(bad_after);
    report(" loop-made="); report_i(loop_made);
    report(" no-error-state="); report_i(no_err_state);
    report(" write-not-refused="); report_i(not_refused);
    report(" refused-after-remount="); report_i(refused_after_remount);
    if (first_bad_at) { report(" first-bad-at="); report_i(first_bad_at); }
    report("\n      old-parent rmdir: removed="); report_i(gt.removed);
    report(" (healthy="); report_i(gt.healthy_removed);
    report(") guard-refused="); report_i(gt.refused);
    report(" (orphan="); report_i(gt.refused_orphan);
    report(" links-surplus-only="); report_i(gt.refused_surplus_only);
    report(") refused-healthy="); report_i(gt.wrong_refuse);
    report(" removed-with-orphan="); report_i(gt.orphan_left);
    report("\n");
    /* 往復 6: 旧親の rmdir は、孤児がいるときは必ず断り、健全なら必ず通す */
    CHECK(gt.refused_orphan > 0);       /* 決裁のガードが孤児に対して実際に働いている */
    CHECK(gt.healthy_removed > 0);      /* 健全な旧親は従来どおり消せる */
    CHECK(gt.wrong_refuse == 0);        /* 健全なディレクトリを断っていない */
    CHECK(gt.orphan_left == 0);         /* 孤児の親を返していない */
    CHECK(two_names == 0);
    CHECK(orphan > 0);                  /* 決裁 1 の代償 (孤児) が実際に起きている */
    CHECK(bad == 0);
    CHECK(bad_after == 0);
    CHECK(loop_made == 0);
    CHECK(no_err_state == 0);
    CHECK(not_refused == 0);
    CHECK(refused_after_remount == 0);
    remount_cold();
    disk_teardown();
}

/* [R5-1b] レビューの輪の反例: 旧名の削除の書き込みを 1 回落とす。往復 4 では
 * ここで 2 名になり、片方の親を D の配下へ動かすと輪ができた。 */
static void case_r5_loop_attempt(void)
{
    MediaReport r;
    Ext2Inode swi;
    u32 sw = 0, etc = 0, root = 0, d = 0, x = 0;
    int rc, f0 = g_failures;
    u8 t;

    report("  [R5-1b] 旧名の削除が落ちる -> (このセッションは断る) -> 再マウント -> 輪を作ろうとする\n");
    fault_done();
    disk_setup();
    if (g_failures != f0) { disk_teardown(); return; }
    CHECK(vfs_mkdir("/sw") == VFS_OK);
    CHECK(vfs_mkdir("/sw/rmd") == VFS_OK);
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/", &root) == EXT2_OK);
    CHECK(ext2_lookup(g_ec, "/sw", &sw) == EXT2_OK);
    CHECK(ext2_lookup(g_ec, "/etc", &etc) == EXT2_OK);
    CHECK(ext2_lookup(g_ec, "/sw/rmd", &d) == EXT2_OK);
    CHECK(ext2_read_inode(g_ec, sw, &swi) == EXT2_OK);

    wfail_arm(g_ec->base_lba + swi.block[0] * 2, 1);
    rc = ext2_rename(g_ec, sw, "rmd", etc, "rmd2");
    wfail_disarm();
    CHECK(g_wfail_fired == 1);
    CHECK(rc == EXT2_ERR_IO);
    CHECK(g_ec->fs_error == 1);
    CHECK(ext2_rename(g_ec, root, "etc", d, "etc2") == EXT2_ERR_ROFS);

    remount_fresh();
    if (!g_ec) return;
    CHECK(ext2_find_entry(g_ec, sw, "rmd", &x, &t) == EXT2_OK);      /* 旧名は残った */
    CHECK(ext2_find_entry(g_ec, etc, "rmd2", &x, &t) == EXT2_ERR_NOTFOUND);
    media_check(&r);
    CHECK_MEDIA(&r);
    e2f_sample("F R5-1b after fault", &r);

    /* D の名前は 1 つなので、/etc を D の配下へ移すのは正当 (輪にならない) */
    CHECK(ext2_rename(g_ec, root, "etc", d, "etc2") == EXT2_OK);
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/sw/rmd/etc2/plain", &x) == EXT2_OK);
    CHECK(ext2_lookup(g_ec, "/sw/rmd/etc2/rmd2", &x) == EXT2_ERR_NOTFOUND);
    /* 逆向き (祖先を子孫の配下へ) は断る */
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/sw/rmd/etc2", &etc) == EXT2_OK);
    CHECK(ext2_rename(g_ec, root, "sw", etc, "sw2") == EXT2_ERR_INVAL);
    media_check(&r);
    CHECK_MEDIA(&r);
    CHECK(r.dir_loop == 0);
    e2f_sample("F R5-1b after rename of /etc under D", &r);
    disk_teardown();
}

/* [R5-6] e2fsck に見せる像 (レビューの 00〜04 を往復 5 のコードで作り直す) */
static void case_r5_e2fsck_images(void)
{
    MediaReport r;
    Ext2Inode di, swi, ei;
    u32 sw = 0, etc = 0, root = 0, d2 = 0, d = 0, x = 0;
    int rc, f0 = g_failures;
    u8 t;

    report("  [R5-6] e2fsck に見せる像 (00 / 01 穴 / 02 旧名削除の失敗 / 02b 孤児 / 03 rmdir / 04 輪の試み)\n");
    fault_done();
    disk_setup();
    if (g_failures != f0) { disk_teardown(); return; }
    CHECK(ext2_sync(g_ec) == EXT2_OK);
    media_check(&r); CHECK_MEDIA(&r);
    e2f_sample("F R5-6 00_base", &r);

    /* 01: 末尾の穴 (繋ぐ書き込みを落とす) -> 短い名前を足しても穴は残る (非 blocker) */
    CHECK(make_full_dir("/dent2", DENT_FULL_IND, &d2, g_dent_name2));
    CHECK(ext2_read_inode(g_ec, d2, &di) == EXT2_OK);
    wfail_arm(g_ec->base_lba + di.block[EXT2_IND_BLOCK] * 2, 1);
    rc = ext2_create(g_ec, d2, g_dent_name2, "x", 1);
    wfail_disarm();
    CHECK(g_wfail_fired == 1);
    CHECK(rc == EXT2_ERR_IO);
    remount_fresh();
    if (!g_ec) return;
    CHECK(ext2_create(g_ec, d2, "after", "y", 1) == EXT2_OK);
    CHECK(ext2_sync(g_ec) == EXT2_OK);
    media_check(&r); CHECK_MEDIA(&r);
    report("      01 hole: dir_hole="); report_i((int)r.dir_hole); report("\n");
    CHECK(r.dir_hole == 1);            /* 「次の追加が埋める」は誤りだった (記録) */
    e2f_sample("F R5-6 01_hole", &r);
    disk_teardown();

    /* 02: 旧名の削除の書き込みを落とす (往復 4 ではここで 2 名になった) */
    disk_setup();
    if (g_failures != f0) { disk_teardown(); return; }
    CHECK(vfs_mkdir("/sw") == VFS_OK);
    CHECK(vfs_mkdir("/sw/rmd") == VFS_OK);
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/", &root) == EXT2_OK);
    CHECK(ext2_lookup(g_ec, "/sw", &sw) == EXT2_OK);
    CHECK(ext2_lookup(g_ec, "/etc", &etc) == EXT2_OK);
    CHECK(ext2_lookup(g_ec, "/sw/rmd", &d) == EXT2_OK);
    CHECK(ext2_read_inode(g_ec, sw, &swi) == EXT2_OK);
    CHECK(ext2_read_inode(g_ec, etc, &ei) == EXT2_OK);
    wfail_arm(g_ec->base_lba + swi.block[0] * 2, 1);
    rc = ext2_rename(g_ec, sw, "rmd", etc, "rmd2");
    wfail_disarm();
    CHECK(rc == EXT2_ERR_IO);
    remount_fresh();
    if (!g_ec) return;
    CHECK(ext2_find_entry(g_ec, etc, "rmd2", &x, &t) == EXT2_ERR_NOTFOUND);
    media_check(&r); CHECK_MEDIA(&r); CHECK(r.dir_multi == 0);
    e2f_sample("F R5-6 02_old_name_delete_failed", &r);

    /* 02b: 新名を載せる書き込み (/etc のブロック 0) を落とす -> 孤児のディレクトリ */
    wfail_arm(g_ec->base_lba + ei.block[0] * 2, 1);
    rc = ext2_rename(g_ec, sw, "rmd", etc, "rmd2");
    wfail_disarm();
    CHECK(g_wfail_fired == 1);
    CHECK(rc == EXT2_ERR_IO);
    remount_fresh();
    if (!g_ec) return;
    CHECK(ext2_find_entry(g_ec, sw, "rmd", &x, &t) == EXT2_ERR_NOTFOUND);
    CHECK(ext2_find_entry(g_ec, etc, "rmd2", &x, &t) == EXT2_ERR_NOTFOUND);
    CHECK(raw_inode_used(d) == 1);
    media_check(&r); CHECK_MEDIA(&r);
    CHECK(r.links_surplus > 0);
    e2f_sample("F R5-6 02b_orphan_dir", &r);

    /* 03: 名前は残っていない (孤児) ので rmdir は何も消さない -> mkdir */
    CHECK(ext2_rmdir(g_ec, etc, "rmd2") == EXT2_ERR_NOTFOUND);
    CHECK(ext2_rmdir(g_ec, sw, "rmd") == EXT2_ERR_NOTFOUND);
    CHECK(vfs_mkdir("/etc/newdir") == VFS_OK);
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/etc/newdir", &x) == EXT2_OK);
    CHECK(x != d);                     /* 孤児の inode は配られない */
    media_check(&r); CHECK_MEDIA(&r);
    e2f_sample("F R5-6 03_rmdir_then_mkdir", &r);

    /* 04: 輪の試み — /etc を /etc/newdir の配下へ */
    CHECK(ext2_rename(g_ec, root, "etc", x, "etc2") == EXT2_ERR_INVAL);
    CHECK(ext2_sync(g_ec) == EXT2_OK);
    media_check(&r); CHECK_MEDIA(&r); CHECK(r.dir_loop == 0);
    e2f_sample("F R5-6 04_loop_attempt", &r);
    disk_teardown();
}

/* [R5-LEGACY] 往復 4 以前のコードが媒体に残した「2 名のディレクトリ」。
 * 決裁 1 はこの状態を**作らない**が、既に媒体にあると OS32 の rmdir はやはり
 * dangling を作る。このセッションでメタデータの I/O エラーが出ていれば、
 * エラー状態がそれを止める (決裁 2)。rename のやり直し経路 (dst_ino == ino) は
 * 2 名を残さず、エラー状態にして断る。 */
static void case_r5_legacy_two_names(void)
{
    MediaReport r;
    Ext2Inode di, ei;
    u32 sw = 0, etc = 0, d = 0;
    int rc, f0 = g_failures;

    report("  [R5-LEGACY] 旧コードの 2 名のディレクトリ: エラー状態が rmdir を止める / やり直し rename は断る\n");
    fault_done();
    disk_setup();
    if (g_failures != f0) { disk_teardown(); return; }
    CHECK(vfs_mkdir("/sw") == VFS_OK);
    CHECK(vfs_mkdir("/sw/rmd") == VFS_OK);
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/sw", &sw) == EXT2_OK);
    CHECK(ext2_lookup(g_ec, "/etc", &etc) == EXT2_OK);
    CHECK(ext2_lookup(g_ec, "/sw/rmd", &d) == EXT2_OK);

    /* 往復 4 の段 1〜3 だけが届いた状態 (レビューの 02_two_names と同じ) */
    CHECK(ext2_read_inode(g_ec, d, &di) == EXT2_OK);
    di.links_count++;
    CHECK(ext2_write_inode(g_ec, d, &di) == EXT2_OK);
    CHECK(ext2_read_inode(g_ec, etc, &ei) == EXT2_OK);
    ei.links_count++;
    CHECK(ext2_write_inode(g_ec, etc, &ei) == EXT2_OK);
    CHECK(ext2_add_entry(g_ec, etc, "rmd2", d, EXT2_FT_DIR) == EXT2_OK);
    CHECK(ext2_sync(g_ec) == EXT2_OK);

    remount_fresh();
    if (!g_ec) return;
    media_check(&r);
    CHECK(r.dir_multi == 1);           /* 往復 5 の検査が見つける (dotdot_bad は
                                        * どちらの親を先に数えるかで 0/1 になる) */
    CHECK(!media_ok(&r));
    e2f_sample("F R5-LEGACY two names (expected inconsistent)", &r);

    /* このセッションでメタデータの I/O エラー -> rmdir はエラー状態が止める */
    CHECK(ext2_read_inode(g_ec, etc, &ei) == EXT2_OK);
    memo_cold();
    fail_arm(g_ec->base_lba + ei.block[0] * 2, 1);
    (void)vfs_path_kind("/etc/plain");
    fail_disarm();
    CHECK(g_fail_fired == 1);
    CHECK(g_ec->fs_error == 1);
    io_reset();
    rc = ext2_rmdir(g_ec, etc, "rmd2");
    CHECK(rc == EXT2_ERR_ROFS);
    CHECK(g_wr_sect == 0);
    media_check(&r);
    CHECK(r.dangling == 0);            /* **媒体で見る**: 解放済み inode を指す名前が無い */
    CHECK(raw_inode_used(d) == 1);

    /* rename のやり直し (dst_ino == ino): 2 名を残して OK、とは言わない */
    remount_fresh();
    if (!g_ec) return;
    io_reset();
    rc = ext2_rename(g_ec, sw, "rmd", etc, "rmd2");
    CHECK(rc == EXT2_ERR_IO);
    CHECK(g_ec->fs_error == 1);
    CHECK(wr_non_state() == 0);
    rc = ext2_rmdir(g_ec, etc, "rmd2");
    CHECK(rc == EXT2_ERR_ROFS);
    media_check(&r);
    CHECK(r.dangling == 0);
    CHECK(raw_inode_used(d) == 1);

    /* 記録のみ ([V4]): エラー状態が無い新しいマウントで旧媒体の 2 名に rmdir を
     * 当てると、往復 4 以前と同じく dangling になる。決裁 1 は OS32 が新たに
     * この状態を作らないことを保証するが、既存の媒体の 2 名は e2fsck が要る。 */
    remount_fresh();
    if (!g_ec) return;
    undo_begin();
    rc = ext2_rmdir(g_ec, etc, "rmd2");
    media_check(&r);
    report("      (record) legacy two names + fresh mount + rmdir: rc="); report_i(rc);
    report(" dangling="); report_i((int)r.dangling); report("\n");
    undo_rollback();
    remount_cold();
    disk_teardown();
}

/* [R5-NOSPC] ディレクトリ rename の新名が NOSPC: 旧名を消した後なので、
 * 旧名・".."・両方の親の links を**元に戻してから** NOSPC を返す
 * (決裁 1 の孤児は I/O 失敗の代償であって、「満杯」で孤児を作ってはいけない)。 */
static void case_r5_dir_rename_nospc(void)
{
    MediaReport before, after;
    u32 sw = 0, full = 0, d = 0, x = 0;
    u16 sw_links, full_links;
    int rc, f0 = g_failures;
    u8 t;

    report("  [R5-NOSPC] ディレクトリ rename: 新名が NOSPC -> 旧名 / '..' / links を戻す\n");
    fault_done();
    disk_setup();
    if (g_failures != f0) { disk_teardown(); return; }
    CHECK(vfs_mkdir("/sw") == VFS_OK);
    CHECK(vfs_mkdir("/sw/rmd") == VFS_OK);
    CHECK(make_full_dir("/full", DENT_FULL, &full, g_dent_name));   /* 次の名前は新ブロック */
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/sw", &sw) == EXT2_OK);
    CHECK(ext2_lookup(g_ec, "/sw/rmd", &d) == EXT2_OK);
    while (ext2_alloc_block(g_ec) >= 0) { }                         /* 空きを使い切る */
    CHECK(g_ec->sb_info.free_blocks_count == 0);
    CHECK(ext2_sync(g_ec) == EXT2_OK);
    sw_links = *(const u16 *)(raw_inode(sw) + 26);
    full_links = *(const u16 *)(raw_inode(full) + 26);
    media_check(&before);
    CHECK_MEDIA(&before);

    rc = ext2_rename(g_ec, sw, "rmd", full, g_dent_name);
    media_check(&after);
    CHECK(rc == EXT2_ERR_NOSPC);
    CHECK(g_ec->fs_error == 0);                    /* NOSPC は I/O エラーではない */
    CHECK_MEDIA(&after);
    CHECK(ext2_find_entry(g_ec, sw, "rmd", &x, &t) == EXT2_OK);     /* 旧名は戻った */
    CHECK(x == d);
    CHECK(ext2_find_entry(g_ec, full, g_dent_name, &x, &t) == EXT2_ERR_NOTFOUND);
    CHECK(*(const u16 *)(raw_inode(sw) + 26) == sw_links);
    CHECK(*(const u16 *)(raw_inode(full) + 26) == full_links);
    CHECK(after.links_surplus == before.links_surplus);             /* 孤児を作っていない */
    {
        u32 par = 0;
        memo_cold();
        CHECK(ext2_lookup(g_ec, "/sw/rmd/..", &par) == EXT2_OK);
        CHECK(par == sw);                          /* ".." も戻った */
    }
    e2f_sample("F R5-NOSPC dir rename rolled back", &after);
    disk_teardown();
}

/* [R5-BADDIR] 壊れた rec_len を media_check が黙って打ち切らず不整合に数えること、
 * そして e2fsck の判定と一致すること。往復 5 のコードはこの状態を作らないので、
 * 媒体を直に壊して作る (検査の側を試す)。 */
static void case_r5_bad_rec_len(void)
{
    MediaReport r;
    Ext2Inode di;
    u32 dno = 0;
    u8 *blk;
    u16 saved;
    int f0 = g_failures;

    report("  [R5-BADDIR] 壊れた rec_len (\".\" を 14 に) -> media_check も e2fsck も不整合\n");
    fault_done();
    disk_setup();
    if (g_failures != f0) { disk_teardown(); return; }
    CHECK(vfs_mkdir("/bd") == VFS_OK);
    CHECK(vfs_write("/bd/a", "A", 1) == VFS_OK);
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/bd", &dno) == EXT2_OK);
    CHECK(ext2_read_inode(g_ec, dno, &di) == EXT2_OK);
    CHECK(ext2_sync(g_ec) == EXT2_OK);
    media_check(&r);
    CHECK_MEDIA(&r);
    CHECK(r.bad_dir == 0);

    blk = raw_blk(di.block[0]);
    saved = *(u16 *)(blk + 4);
    CHECK(saved == 12);
    *(u16 *)(blk + 4) = 14;            /* 4 の倍数でない。鎖が ".." の途中へずれる */
    remount_fresh();
    if (!g_ec) return;
    media_check(&r);
    CHECK(r.bad_dir >= 1);
    CHECK(!media_ok(&r));
    e2f_sample("F R5-BADDIR corrupted rec_len (expected inconsistent)", &r);

    *(u16 *)(blk + 4) = saved;
    remount_fresh();
    if (!g_ec) return;
    media_check(&r);
    CHECK_MEDIA(&r);
    disk_teardown();
}

static void stage_f_round5(void)
{
    report("== 段 F: 往復 5 (ディレクトリ rename は旧名を先に消す / エラー状態) ==\n");
    case_r5_error_state();
    case_r5_link_max();
    case_r5_dir_rename_followups(1);
    case_r5_dir_rename_followups(0);
    case_r5_loop_attempt();
    case_r5_dir_rename_nospc();
    case_r5_e2fsck_images();
    case_r5_legacy_two_names();
    case_r5_bad_rec_len();
}

/* ======================================================================== */
/*  段 G: 往復 6 (Fable 5.1 の往復 6 レビュー、Approve + 非 blocker 8 件)     */
/* ======================================================================== */

/* [R6-GUARD] ユーザー決裁: 空なのに links_count > 2 のディレクトリの rmdir を断る。
 * 健全なディレクトリ (links == 2) を誤って断らないことを、作り方を変えて押さえる。 */
static void case_r6_rmdir_guard(void)
{
    MediaReport r;
    Ext2Inode di;
    u32 root = 0, h = 0, o = 0, x = 0, sdir = 0;
    int f0 = g_failures, k0, rc;

    report("  [R6-GUARD] 空なのに links_count > 2 の rmdir は断る / 健全なものは通す\n");
    fault_done();
    disk_setup();
    if (g_failures != f0) { disk_teardown(); return; }
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/", &root) == EXT2_OK);
    k0 = g_kp_rmdir_refused;

    /* (a) 健全: 作ってすぐ消す */
    CHECK(vfs_mkdir("/h") == VFS_OK);
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/h", &h) == EXT2_OK);
    CHECK(ext2_read_inode(g_ec, h, &di) == EXT2_OK);
    CHECK(di.links_count == EXT2_EMPTY_DIR_LINKS);
    CHECK(vfs_rmdir("/h") == VFS_OK);

    /* (b) 健全: 子を作ってから全部消す (links は 4 -> 2 に戻る) */
    CHECK(vfs_mkdir("/g") == VFS_OK);
    CHECK(vfs_mkdir("/g/a") == VFS_OK);
    CHECK(vfs_mkdir("/g/b") == VFS_OK);
    CHECK(vfs_rmdir("/g") == VFS_ERR_NOTEMPTY);   /* 名前のある子 = 普通の NOTEMPTY */
    CHECK(vfs_rmdir("/g/a") == VFS_OK);
    CHECK(vfs_rmdir("/g/b") == VFS_OK);
    CHECK(vfs_rmdir("/g") == VFS_OK);

    /* (c) 健全: 子ディレクトリを別の親へ rename で移し終えた旧親 */
    CHECK(vfs_mkdir("/m") == VFS_OK);
    CHECK(vfs_mkdir("/m/c") == VFS_OK);
    CHECK(vfs_rename("/m/c", "/etc/c") == VFS_OK);
    CHECK(vfs_rmdir("/m") == VFS_OK);

    /* ここまで (健全なディレクトリ 3 通り) でガードは 1 度も働いていない */
    CHECK(g_kp_rmdir_refused == k0);

    /* (d) 本物の孤児: /o/x を作り、/o の中の名前 "x" だけを消す
     *     (往復 3/4/5 のどの孤児とも同じ形: x の ".." は /o を指したまま、/o の links は 3) */
    CHECK(vfs_mkdir("/o") == VFS_OK);
    CHECK(vfs_mkdir("/o/x") == VFS_OK);
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/o", &o) == EXT2_OK);
    CHECK(ext2_lookup(g_ec, "/o/x", &x) == EXT2_OK);
    CHECK(ext2_delete_entry(g_ec, o, "x") == EXT2_OK);
    remount_fresh();
    if (!g_ec) return;
    media_check(&r);
    CHECK_MEDIA(&r);
    CHECK(orphan_dotdot_refs(o) == 1);
    CHECK(ext2_read_inode(g_ec, o, &di) == EXT2_OK);
    CHECK(di.links_count == 3);
    e2f_sample("G R6-GUARD orphan's .. still counted in parent", &r);

    io_reset();
    rc = ext2_rmdir(g_ec, root, "o");
    CHECK(rc == EXT2_ERR_NOTEMPTY);
    CHECK(g_kp_rmdir_refused == k0 + 1);     /* 通知 1 行 */
    CHECK(g_wr_sect == 0);                   /* 何も書かない */
    CHECK(!g_ec->fs_error);                  /* エラー状態に入れない */
    CHECK(raw_inode_used(o) == 1);           /* inode を返していない */
    memo_cold();
    CHECK(vfs_rmdir("/o") == VFS_ERR_NOTEMPTY);   /* VFS からも同じ番号 */
    /* 書き込みは止まっていない (エラー状態ではない) */
    CHECK(vfs_mkdir("/after") == VFS_OK);
    /* レビュアーの後続: ここで mkdir しても孤児の ".." は生きた別のディレクトリを指さない */
    CHECK(ext2_mkdir(g_ec, root, "reuse") == EXT2_OK);
    remount_fresh();
    if (!g_ec) return;
    media_check(&r);
    CHECK_MEDIA(&r);
    e2f_sample("G R6-GUARD after refused rmdir + mkdir", &r);

    /* (e) 多い側に振れているだけ (孤児なし、links 3) も断る。決裁どおり安全側 */
    CHECK(vfs_mkdir("/s") == VFS_OK);
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/s", &sdir) == EXT2_OK);
    CHECK(ext2_read_inode(g_ec, sdir, &di) == EXT2_OK);
    di.links_count = 3;
    CHECK(ext2_write_inode(g_ec, sdir, &di) == EXT2_OK);
    k0 = g_kp_rmdir_refused;
    CHECK(ext2_rmdir(g_ec, root, "s") == EXT2_ERR_NOTEMPTY);
    CHECK(g_kp_rmdir_refused == k0 + 1);
    di.links_count = EXT2_EMPTY_DIR_LINKS;
    CHECK(ext2_write_inode(g_ec, sdir, &di) == EXT2_OK);
    CHECK(ext2_rmdir(g_ec, root, "s") == EXT2_OK);
    CHECK(g_kp_rmdir_refused == k0 + 1);

    disk_teardown();
}

/* [R6-STAGE6] ディレクトリ rename の段 6 (D の ctime) だけが書けなかったとき、
 * **常に IO** を返す (往復 5 は段 5 の中身次第で OK / IO に分かれていた)。
 * grow = 1: 段 5 が新しいディレクトリブロックを割り当てる (満杯の親へ移す) */
static void case_r6_rename_stage6(int grow)
{
    static char next_name[256];
    MediaReport r;
    Ext2Inode dsti;
    u32 sw = 0, d = 0, dst = 0, lba, x = 0, size_before = 0;
    int count, rc, f0 = g_failures;
    const char *nn;
    u8 t;

    report(grow ? "  [R6-STAGE6g] rename_dir の段 6 だけが落ちる (段 5 がブロックを割り当てる回) -> IO\n"
                : "  [R6-STAGE6] rename_dir の段 6 だけが落ちる (段 5 が割り当てない回) -> IO\n");
    fault_done();
    disk_setup();
    if (g_failures != f0) { disk_teardown(); return; }
    CHECK(vfs_mkdir("/sw") == VFS_OK);
    CHECK(vfs_mkdir("/sw/rmd") == VFS_OK);
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/sw", &sw) == EXT2_OK);
    CHECK(ext2_lookup(g_ec, "/sw/rmd", &d) == EXT2_OK);
    /* grow: 満杯のディレクトリへ、既存ブロックのスラック (最大 244B) に入らない
     * 250 文字の名前で移す = 段 5 が必ず新しいブロックを割り当てる */
    if (grow) {
        CHECK(make_full_dir("/full", DENT_FULL_IND, &dst, next_name));
        nn = next_name;
    } else {
        CHECK(ext2_lookup(g_ec, "/etc", &dst) == EXT2_OK);
        nn = "rmd2";
    }
    CHECK(ext2_read_inode(g_ec, dst, &dsti) == EXT2_OK);
    size_before = dsti.size;
    CHECK(ext2_sync(g_ec) == EXT2_OK);
    if (g_failures != f0) { disk_teardown(); return; }
    lba = lba_of_inode(d);

    /* 空打ち: D の inode ブロックへの書き込み回数を数える。最後の 1 回が段 6 */
    undo_begin();
    remount_cold();
    wfail_arm(lba, 0x7fffffff);
    rc = ext2_rename(g_ec, sw, "rmd", dst, nn);
    count = g_wfail_seen;
    wfail_disarm();
    CHECK(rc == EXT2_OK);
    CHECK(ext2_read_inode(g_ec, dst, &dsti) == EXT2_OK);
    if (grow) CHECK(dsti.size > size_before);    /* 段 5 が本当に割り当てた */
    else CHECK(dsti.size == size_before);         /* 段 5 は割り当てていない */
    undo_rollback();
    remount_cold();
    CHECK(count >= 1);

    wfail_arm(lba, count);
    rc = ext2_rename(g_ec, sw, "rmd", dst, nn);
    wfail_disarm();
    CHECK(g_wfail_fired == 1);
    CHECK(rc == EXT2_ERR_IO);                    /* 段 5 の中身に関わらず IO */
    CHECK(g_ec->fs_error);
    CHECK(ext2_rename(g_ec, sw, "rmd", dst, nn) == EXT2_ERR_ROFS);

    remount_fresh();
    if (!g_ec) return;
    media_check(&r);
    CHECK_MEDIA(&r);
    x = 0;
    CHECK(ext2_find_entry(g_ec, sw, "rmd", &x, &t) == EXT2_ERR_NOTFOUND);
    x = 0;
    CHECK(ext2_find_entry(g_ec, dst, nn, &x, &t) == EXT2_OK && x == d);
    e2f_sample(grow ? "G R6-STAGE6 grow ctime write failed" : "G R6-STAGE6 ctime write failed", &r);
    /* 再マウント後のやり直しは二重に作らない (旧名が無いので NOTFOUND) */
    CHECK(ext2_rename(g_ec, sw, "rmd", dst, nn) == EXT2_ERR_NOTFOUND);
    media_check(&r);
    CHECK_MEDIA(&r);
    disk_teardown();
}

/* [R6-USEDDIRS] rmdir が inode を残した (孤児) ときはディレクトリ数を減らさない */
static void case_r6_rmdir_used_dirs(void)
{
    MediaReport r;
    u32 root = 0, v = 0, grp;
    u16 before;
    int rc, f0 = g_failures;

    report("  [R6-USEDDIRS] rmdir が孤児を残したらディレクトリ数を減らさない / 消せたら 1 減らす\n");
    fault_done();
    disk_setup();
    if (g_failures != f0) { disk_teardown(); return; }
    CHECK(vfs_mkdir("/ud") == VFS_OK);
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/", &root) == EXT2_OK);
    CHECK(ext2_lookup(g_ec, "/ud", &v) == EXT2_OK);
    CHECK(ext2_sync(g_ec) == EXT2_OK);
    CHECK(lba_of_inode(v) != lba_of_inode(root));
    grp = (v - 1) / g_ec->sb_info.inodes_per_group;
    before = g_ec->gd_table[grp].used_dirs;

    wfail_arm_always(lba_of_inode(v));           /* links 0 を書けない = 孤児で残す */
    rc = ext2_rmdir(g_ec, root, "ud");
    wfail_disarm();
    CHECK(g_wfail_fired > 0);
    CHECK(rc < 0);
    CHECK(g_ec->gd_table[grp].used_dirs == before);
    remount_fresh();
    if (!g_ec) return;
    media_check(&r);
    CHECK_MEDIA(&r);
    CHECK(raw_inode_used(v) == 1);
    CHECK(g_ec->gd_table[grp].used_dirs == before);
    e2f_sample("G R6-USEDDIRS orphan kept by rmdir", &r);

    /* 対照: 消せた rmdir は 1 減らし、媒体にも載る */
    CHECK(vfs_mkdir("/ud2") == VFS_OK);
    CHECK(ext2_sync(g_ec) == EXT2_OK);
    before = g_ec->gd_table[grp].used_dirs;
    CHECK(vfs_rmdir("/ud2") == VFS_OK);
    CHECK(g_ec->gd_table[grp].used_dirs == (u16)(before - 1));
    remount_fresh();
    if (!g_ec) return;
    CHECK(g_ec->gd_table[grp].used_dirs == (u16)(before - 1));
    disk_teardown();
}

/* [R6-GROUPS] 複数グループのディスク: ビットマップが読めなければ**次のグループへ
 * 進まず IO**。以前は別のグループへ割り当てて操作が最後まで走り、最後の sync が
 * IO を返していた = 完了しているのに IO。 */
static u8 g_grp_snap[8u * EXT2_BLOCK_SIZE];
static void grp_snapshot(void)
{
    u32 g, n = 0;
    for (g = 1; g < g_ec->num_groups && g < 4; g++) {
        kmemcpy(g_grp_snap + n, raw_blk(g_ec->gd_table[g].block_bitmap), EXT2_BLOCK_SIZE);
        n += EXT2_BLOCK_SIZE;
        kmemcpy(g_grp_snap + n, raw_blk(g_ec->gd_table[g].inode_bitmap), EXT2_BLOCK_SIZE);
        n += EXT2_BLOCK_SIZE;
    }
}
static int grp_unchanged(void)
{
    u32 g, n = 0, i;
    for (g = 1; g < g_ec->num_groups && g < 4; g++) {
        const u8 *bb = raw_blk(g_ec->gd_table[g].block_bitmap);
        const u8 *ib = raw_blk(g_ec->gd_table[g].inode_bitmap);
        for (i = 0; i < EXT2_BLOCK_SIZE; i++) if (bb[i] != g_grp_snap[n + i]) return 0;
        n += EXT2_BLOCK_SIZE;
        for (i = 0; i < EXT2_BLOCK_SIZE; i++) if (ib[i] != g_grp_snap[n + i]) return 0;
        n += EXT2_BLOCK_SIZE;
    }
    return 1;
}

static void case_r6_alloc_groups(void)
{
    static u8 pat[EXT2_BLOCK_SIZE];
    MediaReport r;
    u32 etc = 0, x = 0, bb0, ib0, ng = 0, i, sz = 0, g1_first_blk;
    int rc, b, ino, f0 = g_failures;
    u8 t;

    report("  [R6-GROUPS] 複数グループ: ビットマップが読めなければ次のグループへ進まず IO\n");
    fault_done();
    disk_teardown();
    g_fs_sectors = DISK_GROUPS_FS_SECTORS;
    disk_setup();
    if (g_failures != f0) goto out;
    ng = g_ec->num_groups;
    report("      groups="); report_i((int)ng); report("\n");
    CHECK(ng >= 3);
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/etc", &etc) == EXT2_OK);
    for (i = 0; i < sizeof(pat); i++) pat[i] = (u8)(i * 13 + 5);
    CHECK(ext2_vfs_write(g_ec, "/etc/app", pat, EXT2_BLOCK_SIZE) == VFS_OK);
    CHECK(ext2_sync(g_ec) == EXT2_OK);
    CHECK(g_ec->gd_table[0].free_blocks > 0 && g_ec->gd_table[0].free_inodes > 0);
    CHECK(g_ec->gd_table[1].free_blocks > 0 && g_ec->gd_table[1].free_inodes > 0);
    bb0 = g_ec->base_lba + g_ec->gd_table[0].block_bitmap * 2;
    ib0 = g_ec->base_lba + g_ec->gd_table[0].inode_bitmap * 2;
    g1_first_blk = g_ec->sb_info.first_data_block + g_ec->sb_info.blocks_per_group;
    if (g_failures != f0) goto out;
    media_check(&r);
    CHECK_MEDIA(&r);
    e2f_sample("G R6-GROUPS fresh multi-group format", &r);

    /* (1) グループ 0 のブロックビットマップが読めない: create */
    remount_cold();
    grp_snapshot();
    fail_arm_always(bb0);
    rc = ext2_create(g_ec, etc, "g1", "x", 1);
    fail_disarm();
    CHECK(g_fail_fired > 0);
    CHECK(rc == EXT2_ERR_IO);
    CHECK(g_ec->fs_error);
    CHECK(grp_unchanged());                      /* 別のグループに割り当てていない */
    remount_fresh();
    if (!g_ec) goto out;
    CHECK(ext2_find_entry(g_ec, etc, "g1", &x, &t) == EXT2_ERR_NOTFOUND);   /* 完了していない */
    media_check(&r);
    CHECK_MEDIA(&r);
    e2f_sample("G R6-GROUPS create, group 0 block bitmap unreadable", &r);

    /* (2) グループ 0 の inode ビットマップが読めない: create */
    remount_cold();
    grp_snapshot();
    fail_arm_always(ib0);
    rc = ext2_create(g_ec, etc, "g2", "x", 1);
    fail_disarm();
    CHECK(g_fail_fired > 0);
    CHECK(rc == EXT2_ERR_IO);
    CHECK(grp_unchanged());
    remount_fresh();
    if (!g_ec) goto out;
    CHECK(ext2_find_entry(g_ec, etc, "g2", &x, &t) == EXT2_ERR_NOTFOUND);
    media_check(&r);
    CHECK_MEDIA(&r);

    /* (3) mkdir (inode ビットマップ) */
    remount_cold();
    grp_snapshot();
    fail_arm_always(ib0);
    rc = ext2_mkdir(g_ec, etc, "g3");
    fail_disarm();
    CHECK(g_fail_fired > 0);
    CHECK(rc == EXT2_ERR_IO);
    CHECK(grp_unchanged());
    remount_fresh();
    if (!g_ec) goto out;
    CHECK(ext2_find_entry(g_ec, etc, "g3", &x, &t) == EXT2_ERR_NOTFOUND);
    media_check(&r);
    CHECK_MEDIA(&r);
    e2f_sample("G R6-GROUPS mkdir, group 0 inode bitmap unreadable", &r);

    /* (4) 追記に新しいブロックが要る (ブロックビットマップ) -> 0 ではなく IO */
    remount_cold();
    grp_snapshot();
    fail_arm_always(bb0);
    rc = ext2_vfs_write_stream(g_ec, "/etc/app", pat, 10, EXT2_BLOCK_SIZE);
    fail_disarm();
    CHECK(g_fail_fired > 0);
    CHECK(rc == VFS_ERR_IO);
    CHECK(grp_unchanged());
    remount_fresh();
    if (!g_ec) goto out;
    CHECK(ext2_vfs_get_size(g_ec, "/etc/app", &sz) == VFS_OK);
    CHECK(sz == EXT2_BLOCK_SIZE);
    media_check(&r);
    CHECK_MEDIA(&r);

    /* (5) 対照: グループ 0 が**満杯**なら従来どおり次のグループへ進む */
    remount_cold();
    g_ec->gd_table[0].free_blocks = 0;
    b = ext2_alloc_block(g_ec);
    CHECK(b > 0 && (u32)b >= g1_first_blk);
    if (b > 0) CHECK(ext2_free_block(g_ec, (u32)b) == EXT2_OK);
    remount_cold();
    g_ec->gd_table[0].free_inodes = 0;
    ino = ext2_alloc_inode(g_ec);
    CHECK(ino > 0 && (u32)ino > g_ec->sb_info.inodes_per_group);
    if (ino > 0) CHECK(ext2_free_inode(g_ec, (u32)ino) == EXT2_OK);
    CHECK(!g_ec->fs_error);

    /* (6) NOSPC と IO を区別して返す */
    remount_cold();
    for (i = 0; i < ng; i++) { g_ec->gd_table[i].free_blocks = 0; g_ec->gd_table[i].free_inodes = 0; }
    CHECK(ext2_alloc_block(g_ec) == EXT2_ERR_NOSPC);
    CHECK(ext2_alloc_inode(g_ec) == EXT2_ERR_NOSPC);
    CHECK(!g_ec->fs_error);
    remount_cold();
    fail_arm_always(bb0);
    CHECK(ext2_alloc_block(g_ec) == EXT2_ERR_IO);
    fail_disarm();
    remount_cold();
    fail_arm_always(ib0);
    CHECK(ext2_alloc_inode(g_ec) == EXT2_ERR_IO);
    fail_disarm();
    remount_fresh();
    if (!g_ec) goto out;
    media_check(&r);
    CHECK_MEDIA(&r);
    e2f_sample("G R6-GROUPS after all scenarios", &r);

out:
    fail_disarm();
    disk_teardown();
    g_fs_sectors = DISK_FS_SECTORS;
}

static void stage_g_round6(void)
{
    report("== 段 G: 往復 6 (rmdir のガード / 複数グループの割り当て / rename 段 6 / used_dirs) ==\n");
    case_r6_rmdir_guard();
    case_r6_rename_stage6(0);
    case_r6_rename_stage6(1);
    case_r6_rmdir_used_dirs();
    case_r6_alloc_groups();
    stage_c2_orphan_parent_sweeps();
}

/* 定義は下の「段 X3」節 (票 H2 §2-2 の置き換え rename)。 */
static void stage_x3_replace(void);

static void stage_a(void)
{
    report("== 段 A: 実物の ext2 (RAM ディスク) + 実物の vfs_open ==\n");
    disk_setup();
    if (g_failures) { disk_teardown(); return; }

    case_bmap_contract();

    /* O_CREAT あり / なし x O_TRUNC あり / なし の 4 通り */
    case_indirect_read_failure(O_RDONLY, "間接ブロック失敗 / O_CREAT 無 O_TRUNC 無");
    case_indirect_read_failure(O_WRONLY | O_CREAT,
                               "間接ブロック失敗 / O_CREAT 有 O_TRUNC 無");
    case_indirect_read_failure(O_WRONLY | O_TRUNC,
                               "間接ブロック失敗 / O_CREAT 無 O_TRUNC 有");
    case_indirect_read_failure(O_WRONLY | O_CREAT | O_TRUNC,
                               "間接ブロック失敗 / O_CREAT 有 O_TRUNC 有");

    case_size_failure(O_RDONLY, "サイズ取得失敗 / O_CREAT 無 O_TRUNC 無");
    case_size_failure(O_WRONLY | O_CREAT,
                      "サイズ取得失敗 / O_CREAT 有 O_TRUNC 無 (**無言のデータ消失**)");
    case_size_failure(O_WRONLY | O_TRUNC,
                      "サイズ取得失敗 / O_CREAT 無 O_TRUNC 有");
    case_size_failure(O_WRONLY | O_CREAT | O_TRUNC,
                      "サイズ取得失敗 / O_CREAT 有 O_TRUNC 有");

    case_sqlite_size_failure();
    case_write_file_failure();
    case_write_stream_failure();
    case_other_bmap_callers();
    case_mkdir_existence_failure();
    case_create_existence_failure();
    case_rename_dest_failure();
    case_write_stream_inode_failure();
    case_reread_after_probe();
    case_free_block_bitmap_failure();
    case_rewrite_new_block_failure();
    case_create_cleanup_failure();
    case_create_inode_write_ambiguous();
    case_unlink_release_failure();
    case_rmdir_release_failure();
    case_trailing_sync_failure();
    case_normal_paths();

    disk_teardown();

    /* 段 C: 失敗の位置を総当たりで動かし、毎回媒体を検査する */
    stage_c_sweeps();

    /* 空きを使い切った状態が要るので、別の新しいディスクで */
    case_create_add_entry_nospc();

    /* 段 E: レビュー (往復 3) の反例を本試験に取り込む */
    stage_e_review_cases();

    /* 段 F: 往復 5 */
    stage_f_round5();

    /* 段 G: 往復 6 */
    stage_g_round6();

    /* 段 H: 独立した 2 か所の失敗 (TASK_FS_TYPE §2-6 の「二重故障」) */
    stage_h_pair_sweeps();

    /* 段 X3: 置き換え rename の公開処理 (票 H2 §2-2 / X3b X3c X3d) */
    stage_x3_replace();
}

/* ======================================================================== */
/*  段 B: 合成 VfsOps — write_file の**呼び出し回数**で押さえる             */
/* ======================================================================== */

static int  s_stat_rc;
static int  s_stat_is_dir;
static int  s_size_rc;
static int  s_size_calls;
static int  s_write_rc;
static int  s_write_calls;
static u32  s_size_value;
/* 合成 FS が持つ「中身」。O_TRUNC / 作成が本当に走ったかを長さで見る。 */
static u32  s_content_len;

static int s_stat(void *ctx, const char *path, OS32_Stat *buf)
{
    (void)ctx; (void)path;
    kmemset(buf, 0, sizeof(OS32_Stat));
    if (s_stat_rc != VFS_OK) return s_stat_rc;
    buf->st_mode = (u16)(s_stat_is_dir ? (OS_S_IFDIR | 0755) : (OS_S_IFREG | 0644));
    buf->st_size = s_content_len;
    return VFS_OK;
}

static int s_get_file_size(void *ctx, const char *path, u32 *size)
{
    (void)ctx; (void)path;
    s_size_calls++;
    if (s_size_rc != VFS_OK) return s_size_rc;
    *size = s_size_value;
    return VFS_OK;
}

static int s_write_file(void *ctx, const char *path, const void *data, u32 size)
{
    (void)ctx; (void)path; (void)data;
    s_write_calls++;
    if (s_write_rc != VFS_OK) return s_write_rc;
    s_content_len = size;                 /* 0 バイト書き込み = 中身が消える */
    return VFS_OK;
}

static int s_ctx_tag = 1;
static void *s_mount(int dev) { (void)dev; return &s_ctx_tag; }
static void s_umount(void *ctx) { (void)ctx; }
static int s_is_mounted(void *ctx) { (void)ctx; return 1; }

static VfsOps s_ops;

static void synth_reset(void)
{
    vfs_tables_reset();

    s_stat_rc = VFS_OK;
    s_stat_is_dir = 0;
    s_size_rc = VFS_OK;
    s_size_calls = 0;
    s_write_rc = VFS_OK;
    s_write_calls = 0;
    s_size_value = 16;
    s_content_len = 16;

    kmemset(&s_ops, 0, sizeof(s_ops));
    s_ops.name = "synth";
    s_ops.mount = s_mount;
    s_ops.umount = s_umount;
    s_ops.is_mounted = s_is_mounted;
    s_ops.get_file_size = s_get_file_size;
    s_ops.write_file = s_write_file;
    s_ops.stat = s_stat;

    vfs_register_fs(&s_ops);
    if (vfs_mount("/synth", "hostdrv", "synth") != VFS_OK) {
        report("  (harness) synth vfs_mount failed\n");
        g_failures++;
    }
}

/* 「通常ファイルと確認済み → サイズ取得だけが一度失敗」の 4 通り。
 * ここでは write_file の**呼び出し回数**をそのまま数えられる。 */
static void synth_size_failure(int mode, int size_rc, const char *label)
{
    int fd;

    report("  [B1] "); report(label); report("\n");

    synth_reset();
    s_stat_rc = VFS_OK;
    s_stat_is_dir = 0;                    /* 種別は「通常ファイル」で確定 */
    s_size_rc = size_rc;                  /* サイズ取得だけが失敗する */

    fd = vfs_open("/synth/f", mode);

    CHECK(fd < 0);                        /* FD を発行しない */
    CHECK(fd == size_rc);                 /* そのエラーをそのまま返す */
    CHECK(s_write_calls == 0);            /* **write_file を呼ばない** */
    CHECK(s_content_len == 16);           /* 中身が残っている */
    CHECK(any_fd_open() == 0);
}

static void synth_trunc_write_failure(void)
{
    int fd;

    report("  [B2] O_TRUNC の write_file が失敗したら open しない\n");

    synth_reset();
    s_write_rc = VFS_ERR_ISDIR;           /* ext2 の一括書き込みが持つ拒否 */
    fd = vfs_open("/synth/f", O_WRONLY | O_TRUNC);
    CHECK(fd == VFS_ERR_ISDIR);           /* 失敗をそのまま返す */
    CHECK(s_write_calls == 1);
    CHECK(any_fd_open() == 0);

    report("  [B3] O_CREAT の write_file が失敗したら open しない\n");
    synth_reset();
    s_size_rc = VFS_ERR_NOTFOUND;
    s_stat_rc = VFS_ERR_NOTFOUND;
    s_write_rc = VFS_ERR_NOSPC;
    fd = vfs_open("/synth/new", O_WRONLY | O_CREAT);
    CHECK(fd == VFS_ERR_NOSPC);
    CHECK(s_write_calls == 1);
    CHECK(any_fd_open() == 0);
}

static void synth_sqlite(void)
{
    VfsSqliteCookie ck;
    VfsSqliteLease lease;
    int rc;

    report("  [B4] vfs_open_sqlite も作成へ進まない\n");

    ck.group_index = 0;
    ck.generation = 1;

    synth_reset();
    s_size_rc = VFS_ERR_IO;
    kmemset(&lease, 0, sizeof(lease));
    rc = vfs_open_sqlite("/synth/f", O_RDWR | O_CREAT, 0, &ck, 0, &lease);
    CHECK(rc == VFS_ERR_IO);
    CHECK(s_write_calls == 0);
    CHECK(s_content_len == 16);
    CHECK(any_fd_open() == 0);

    /* 本当に不存在なら従来どおり作れる */
    synth_reset();
    s_stat_rc = VFS_ERR_NOTFOUND;
    s_size_rc = VFS_ERR_NOTFOUND;
    kmemset(&lease, 0, sizeof(lease));
    rc = vfs_open_sqlite("/synth/new", O_RDWR | O_CREAT, 0, &ck, 0, &lease);
    CHECK(rc == VFS_OK);
    CHECK(s_write_calls == 1);
    CHECK(lease.fd >= 3);
}

static void synth_normal(void)
{
    int fd;

    report("  [B5] 合成ドライバでの正常系\n");

    synth_reset();
    fd = vfs_open("/synth/f", O_RDONLY);
    CHECK(fd >= 3);
    CHECK(s_write_calls == 0);

    synth_reset();
    s_stat_rc = VFS_ERR_NOTFOUND;
    s_size_rc = VFS_ERR_NOTFOUND;
    fd = vfs_open("/synth/new", O_WRONLY | O_CREAT);
    CHECK(fd >= 3);
    CHECK(s_write_calls == 1);            /* 空ファイルを 1 度書いて作る */

    synth_reset();
    s_stat_rc = VFS_ERR_NOTFOUND;
    s_size_rc = VFS_ERR_NOTFOUND;
    CHECK(vfs_open("/synth/new", O_RDONLY) == VFS_ERR_NOTFOUND);
    CHECK(s_write_calls == 0);

    synth_reset();
    fd = vfs_open("/synth/f", O_WRONLY | O_TRUNC);
    CHECK(fd >= 3);
    CHECK(s_write_calls == 1);
    CHECK(s_content_len == 0);            /* 本当に切り詰まる */

    synth_reset();
    s_stat_is_dir = 1;
    CHECK(vfs_open("/synth/d", O_WRONLY | O_CREAT | O_TRUNC) == VFS_ERR_ISDIR);
    CHECK(s_write_calls == 0);

    /* サイズ取得がディレクトリを断ったら、それは「無い」ではなく DIR
     * (stat を持たないドライバのプローブ、③ の受け手) */
    synth_reset();
    s_ops.stat = 0;
    vfs_tables_reset();
    vfs_register_fs(&s_ops);
    CHECK(vfs_mount("/synth", "hostdrv", "synth") == VFS_OK);
    s_size_rc = VFS_ERR_ISDIR;
    CHECK(vfs_path_kind("/synth/d") == VFS_KIND_DIR);
    CHECK(vfs_open("/synth/d", O_WRONLY | O_CREAT) == VFS_ERR_ISDIR);
    CHECK(s_write_calls == 0);
}

/* HostDrv の get_file_size が「サイズと名乗ってよいか」(③)。
 * NP21/W の HostDrv は NON_DIRECTORY_FILE を付けずに開くので**ディレクトリでも
 * 成功し、NT はディレクトリにもサイズを返す**。 */
static void stage_c(void)
{
    report("== 段 C: HostDrv のサイズ取得の純規則 (fs/hostdrv_stat_rules.inc) ==\n");
    report("  [C1] hdrv_size_result\n");

    /* 通常ファイル: 問い合わせが通っていればサイズを返してよい */
    CHECK(hdrv_size_result(0, 0) == 0);

    /* **ディレクトリには答えない** */
    CHECK(hdrv_size_result(0, 1) == OS32_ERR_ISDIR);
    CHECK(hdrv_size_result(0, 1) != 0);

    /* 問い合わせが通っていない = 種別もサイズも分からない。
     * 「サイズ 0 の通常ファイル」と名乗らない。 */
    CHECK(hdrv_size_result(-1, 0) == OS32_ERR_IO);
    CHECK(hdrv_size_result(-1, 1) == OS32_ERR_IO);
    CHECK(hdrv_size_result(1, 0) == OS32_ERR_IO);

    /* NP21/W は Directory を FileStandardInformation に埋める
     * (np21w-src/src/generic/hostdrvnt.c)。値は 0 / 1 だが、
     * 非 0 ならディレクトリとして扱う。 */
    CHECK(hdrv_size_result(0, 2) == OS32_ERR_ISDIR);
}

static void stage_b(void)
{
    report("== 段 B: 合成 VfsOps (write_file の呼び出し回数で押さえる) ==\n");

    synth_size_failure(O_RDONLY, VFS_ERR_IO,
                       "O_CREAT 無 O_TRUNC 無 / サイズ取得 IO");
    synth_size_failure(O_WRONLY | O_CREAT, VFS_ERR_IO,
                       "O_CREAT 有 O_TRUNC 無 / サイズ取得 IO (**最重要**)");
    synth_size_failure(O_WRONLY | O_TRUNC, VFS_ERR_IO,
                       "O_CREAT 無 O_TRUNC 有 / サイズ取得 IO");
    synth_size_failure(O_WRONLY | O_CREAT | O_TRUNC, VFS_ERR_IO,
                       "O_CREAT 有 O_TRUNC 有 / サイズ取得 IO");
    /* NOTFOUND 以外なら何でも同じ — 「無い」以外は作らない */
    synth_size_failure(O_WRONLY | O_CREAT, VFS_ERR_NOSPC,
                       "O_CREAT 有 / サイズ取得 NOSPC");
    synth_size_failure(O_WRONLY | O_CREAT, VFS_ERR_NOTDIR,
                       "O_CREAT 有 / サイズ取得 NOTDIR");
    synth_size_failure(O_WRONLY | O_CREAT, VFS_ERR_INVAL,
                       "O_CREAT 有 / サイズ取得 INVAL");

    synth_trunc_write_failure();
    synth_sqlite();
    synth_normal();
}

/* ======================================================================== */
/*  段 X3: 通常ファイル同士の置き換え rename (票 H2 §2-2)                     */
/*                                                                          */
/*  X3b  公開処理 (段 2) の 3 値を、**フィールドが前半 / 後半セクタ**の配置 x  */
/*       3 つの落ち方で確かめる:                                             */
/*         (i)   媒体に届く前に失敗   -> **未公開**。D を解放しない           */
/*         (ii)  媒体に届いた後に失敗 -> 読み直して **公開済み**、段 3〜6 へ   */
/*         (iii) 書き込み失敗 + 読み直しも失敗 -> **不明**。書き込み禁止にし、 */
/*               段 3〜5 を一切実行しない (D を解放しない)                    */
/*  X3c  段 2 の後に ext2_resolve_path を通す経路が古い D を返さない          */
/*  X3d  掃除の ext2_unlink で links_count の書き込みだけ失敗 -> OK を返さない */
/* ======================================================================== */

#define X3_PAD_LEN 244          /* rec_len 252。2 件で 528 = 2 セクタ目の頭 */
#define X3_OLD     "OLDOLDOLD"
#define X3_OLD_LEN 9
#define X3_NEW     "NEWNEWNEWNEW"
#define X3_NEW_LEN 12

/* /x に "dst" (置き換えられる側 D) と "src" (置き換える側 S) を作る。
 * second = 1 なら先に 244 文字の詰め物を 2 件入れて、dst のエントリが
 * **ブロックの後半セクタ**に載るようにする。 */
static int x3_make(int second, u32 *dir_ino, u32 *d_ino, u32 *s_ino,
                   u32 *phys, u32 *pos)
{
    static char pad[X3_PAD_LEN + 1];
    u8 ftype = 0;
    int k, i;

    if (ext2_vfs_mkdir(g_ec, "/x") != VFS_OK) return 0;
    memo_cold();
    if (ext2_lookup(g_ec, "/x", dir_ino) != EXT2_OK) return 0;

    if (second) {
        for (k = 0; k < 2; k++) {
            for (i = 0; i < X3_PAD_LEN; i++) pad[i] = (char)('A' + k);
            pad[X3_PAD_LEN] = '\0';
            if (ext2_create(g_ec, *dir_ino, pad, "", 0) != EXT2_OK) return 0;
        }
    }
    if (ext2_create(g_ec, *dir_ino, "dst", X3_OLD, X3_OLD_LEN) != EXT2_OK) return 0;
    if (ext2_create(g_ec, *dir_ino, "src", X3_NEW, X3_NEW_LEN) != EXT2_OK) return 0;
    if (ext2_find_entry_loc(g_ec, *dir_ino, "dst", d_ino, &ftype, phys, pos)
        != EXT2_OK) return 0;
    if (ext2_find_entry(g_ec, *dir_ino, "src", s_ino, (u8 *)0) != EXT2_OK) return 0;
    if (ext2_sync(g_ec) != EXT2_OK) return 0;
    /* 配置が狙いどおりか (前半 = 0 / 後半 = 1) */
    return ((*pos / EXT2_SECTOR_SIZE) == (second ? 1u : 0u)) ? 1 : 0;
}

/* dst エントリの inode フィールドを含むセクタの LBA */
static u32 x3_sector_lba(u32 phys, u32 pos)
{
    return g_ec->base_lba + phys * 2 + pos / EXT2_SECTOR_SIZE;
}

#define X3_MODE_BEFORE  0
#define X3_MODE_LANDED  1
#define X3_MODE_UNKNOWN 2

static void case_x3b(int second, int mode, const char *label)
{
    MediaReport r;
    u32 dir = 0, d_ino = 0, s_ino = 0, phys = 0, pos = 0, lba, seen = 0;
    int rc;
    int f0 = g_failures;

    report("  [X3b] "); report(label); report("\n");
    disk_setup();
    if (g_failures != f0) { disk_teardown(); return; }
    if (!x3_make(second, &dir, &d_ino, &s_ino, &phys, &pos)) {
        report("  (harness) x3_make failed\n");
        g_failures++;
        disk_teardown();
        return;
    }
    lba = x3_sector_lba(phys, pos);

    if (mode == X3_MODE_BEFORE) {
        wfail_arm_always(lba);                  /* 届く前に失敗 */
    } else if (mode == X3_MODE_LANDED) {
        wfail_arm_landed(lba, 1);               /* 1 回目だけ届いてから失敗 */
    } else {
        wfail_arm_always(lba);
        g_wfail_then_rfail = 1;                 /* 読み直しも落とす */
    }

    rc = ext2_rename(g_ec, dir, "src", dir, "dst");
    wfail_disarm();
    fail_disarm();

    /* 宛先の名前は**どの落ち方でも**消えない */
    CHECK(raw_find_name(dir, "dst", &seen) == 1);

    if (mode == X3_MODE_LANDED) {
        /* (ii) 読み直しで S を見て公開済みと判断し、段 3〜6 まで進む */
        CHECK(rc == EXT2_OK);
        CHECK(seen == s_ino);
        CHECK(raw_find_name(dir, "src", (u32 *)0) == 0);   /* 段 3 が済んだ */
        CHECK(raw_links(s_ino) == 1);                      /* 段 4 が済んだ */
        CHECK(raw_inode_used(d_ino) == 0);                 /* 段 5: D は解放 */
    } else {
        /* (i) 未公開 / (iii) 不明。どちらも **D を解放しない** */
        CHECK(rc != EXT2_OK);
        CHECK(raw_inode_used(d_ino) == 1);
        CHECK(raw_find_name(dir, "src", (u32 *)0) == 1);   /* 移動元も残る */
        if (mode == X3_MODE_BEFORE) {
            CHECK(seen == d_ino);               /* 旧 inode のまま */
        } else {
            CHECK(seen == d_ino || seen == s_ino);   /* どちらでも整合する */
            CHECK(g_ec->fs_error == 1);         /* 不明 -> 書き込み禁止 */
        }
    }

    media_check(&r);
    CHECK_MEDIA(&r);
    disk_teardown();
}

/* X3c: 段 2 の後に ext2_resolve_path を通す経路が古い D を返さない。
 * 段 3 (移動元の名前を消す) を落とすと ext2_delete_entry の ns_touch が
 * 走らないので、**段 0 の ext2_ns_touch だけ**が記憶を捨てる役になる。 */
static void case_x3c(void)
{
    MediaReport r;
    OS32_Stat st;
    u32 a_dir = 0, b_dir = 0, d_ino = 0, s_ino = 0, phys = 0, pos = 0;
    u8 ftype = 0;
    int rc, fd;
    int f0 = g_failures;

    report("  [X3c] 段 2 の後に古い D を返さない (ext2_ns_touch)\n");
    disk_setup();
    if (g_failures != f0) { disk_teardown(); return; }

    CHECK(ext2_vfs_mkdir(g_ec, "/a") == VFS_OK);
    CHECK(ext2_vfs_mkdir(g_ec, "/b") == VFS_OK);
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/a", &a_dir) == EXT2_OK);
    CHECK(ext2_lookup(g_ec, "/b", &b_dir) == EXT2_OK);
    CHECK(ext2_create(g_ec, a_dir, "src", X3_NEW, X3_NEW_LEN) == EXT2_OK);
    CHECK(ext2_create(g_ec, b_dir, "dst", X3_OLD, X3_OLD_LEN) == EXT2_OK);
    CHECK(ext2_find_entry(g_ec, a_dir, "src", &s_ino, (u8 *)0) == EXT2_OK);
    CHECK(ext2_find_entry_loc(g_ec, b_dir, "dst", &d_ino, &ftype, &phys, &pos)
          == EXT2_OK);
    CHECK(ext2_sync(g_ec) == EXT2_OK);
    if (g_failures != f0) { disk_teardown(); return; }

    /* 記憶を温める: これで memo["/b/dst"] = D になる */
    CHECK(ext2_vfs_stat(g_ec, "/b/dst", &st) == VFS_OK);
    CHECK(st.st_ino == d_ino);

    /* 段 3 (= /a のブロックへの書き込み) だけを落とす */
    {
        Ext2Inode ai;
        CHECK(ext2_read_inode(g_ec, a_dir, &ai) == EXT2_OK);
        wfail_arm_always(g_ec->base_lba + ai.block[0] * 2);
    }
    rc = ext2_rename(g_ec, a_dir, "src", b_dir, "dst");
    wfail_disarm();
    CHECK(rc != EXT2_OK);                       /* 後始末が落ちた */

    /* **記憶が残っていると D (解放済み) を返す** */
    CHECK(ext2_vfs_stat(g_ec, "/b/dst", &st) == VFS_OK);
    CHECK(st.st_ino == s_ino);
    CHECK(st.st_size == X3_NEW_LEN);

    fd = vfs_open("/b/dst", O_RDONLY);
    CHECK(fd >= 3);
    if (fd >= 3) {
        CHECK(vfs_get_size(fd) == X3_NEW_LEN);
        vfs_close(fd);
    }
    {
        u32 sz = 0;
        CHECK(ext2_vfs_get_size(g_ec, "/b/dst", &sz) == VFS_OK);
        CHECK(sz == X3_NEW_LEN);
    }

    media_check(&r);
    CHECK_MEDIA(&r);
    disk_teardown();
}

/* X3d: 掃除の ext2_unlink で links_count の書き込みだけ失敗する。
 * 名前が消えたのに links が減っていない回に **EXT2_OK を返してはいけない**
 * (往復 2 所見 5)。書き込みの位置を総当たりして、その形が起きた回を見る。 */
static void case_x3d(void)
{
    u32 dir = 0, ino = 0;
    int at, rc, hits = 0, wrong = 0, runs = 0;
    int f0 = g_failures;

    report("  [X3d] ext2_unlink: links の書き込みだけ失敗 -> OK を返さない\n");
    disk_setup();
    if (g_failures != f0) { disk_teardown(); return; }

    CHECK(ext2_vfs_mkdir(g_ec, "/d") == VFS_OK);
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/d", &dir) == EXT2_OK);
    CHECK(ext2_create(g_ec, dir, "f", X3_OLD, X3_OLD_LEN) == EXT2_OK);
    CHECK(ext2_find_entry(g_ec, dir, "f", &ino, (u8 *)0) == EXT2_OK);
    /* 2 つ目の名前を足して links_count = 2 にする (段 3 で止まった媒体の形) */
    {
        Ext2Inode fi;
        CHECK(ext2_read_inode(g_ec, ino, &fi) == EXT2_OK);
        fi.links_count = 2;
        CHECK(ext2_write_inode(g_ec, ino, &fi) == EXT2_OK);
    }
    CHECK(ext2_add_entry(g_ec, dir, "g", ino, EXT2_FT_REG_FILE) == EXT2_OK);
    CHECK(ext2_sync(g_ec) == EXT2_OK);
    if (g_failures != f0) { disk_teardown(); return; }

    for (at = 1; ; at++) {
        int fired;
        if (at > SWEEP_MAX) {
            report("  (harness) x3d too long\n");
            g_failures++;
            break;
        }
        undo_begin();
        remount_cold();
        sw_arm(at, 0, SW_KIND_WRITE);
        rc = ext2_unlink(g_ec, dir, "g");
        fired = g_sw_fired;
        sw_disarm();
        runs++;

        /* 名前は消えたのに links が 2 のまま = 直せていない */
        if (raw_find_name(dir, "g", (u32 *)0) == 0 && raw_inode_used(ino) &&
            raw_links(ino) == 2) {
            hits++;
            if (rc == EXT2_OK) wrong++;
        }
        undo_rollback();
        if (!fired) break;
    }
    remount_cold();

    report("          runs="); report_i(runs);
    report(" links-not-fixed="); report_i(hits);
    report(" wrongly-ok="); report_i(wrong);
    report("\n");
    check_at(hits > 0,
             "X3d: the 'name gone but links unchanged' case actually occurs",
             __LINE__);
    check_at(wrong == 0,
             "X3d: ext2_unlink never reports OK when links stay behind",
             __LINE__);
    disk_teardown();
}

/* X4 (票 H2 §4-1): 置き換えたあとの旧 inode D の始末。
 *   links 1 -> 0 … **解放される** (X3b の LANDED でも見ている)
 *   links 2 -> 1 … **残る**。もう一つの名前がまだ D を指しているので、
 *                  解放したら生きている名前が解放済み inode を指す。
 * どちらも交差リンク (media_check の freed_ref / dup_ref / dangling) が無いこと。 */
static void case_x3_links2(void)
{
    MediaReport r;
    u32 dir = 0, d_ino = 0, s_ino = 0, seen = 0, other = 0;
    int f0 = g_failures;

    report("  [X4] 置き換え: 旧 inode の links 1 -> 0 は解放、2 -> 1 は残す\n");

    /* --- links 1 -> 0 (解放される) --- */
    disk_setup();
    if (g_failures != f0) { disk_teardown(); return; }
    CHECK(ext2_vfs_mkdir(g_ec, "/k") == VFS_OK);
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/k", &dir) == EXT2_OK);
    CHECK(ext2_create(g_ec, dir, "dst", X3_OLD, X3_OLD_LEN) == EXT2_OK);
    CHECK(ext2_create(g_ec, dir, "src", X3_NEW, X3_NEW_LEN) == EXT2_OK);
    CHECK(ext2_find_entry(g_ec, dir, "dst", &d_ino, (u8 *)0) == EXT2_OK);
    CHECK(ext2_find_entry(g_ec, dir, "src", &s_ino, (u8 *)0) == EXT2_OK);
    CHECK(ext2_rename(g_ec, dir, "src", dir, "dst") == EXT2_OK);
    CHECK(raw_find_name(dir, "dst", &seen) == 1);
    CHECK(seen == s_ino);
    CHECK(raw_find_name(dir, "src", (u32 *)0) == 0);
    CHECK(raw_links(s_ino) == 1);
    CHECK(raw_inode_used(d_ino) == 0);          /* links 1 -> 0 なので解放 */
    media_check(&r);
    CHECK_MEDIA(&r);
    disk_teardown();

    /* --- links 2 -> 1 (残す) --- */
    f0 = g_failures;
    disk_setup();
    if (g_failures != f0) { disk_teardown(); return; }
    CHECK(ext2_vfs_mkdir(g_ec, "/k") == VFS_OK);
    memo_cold();
    CHECK(ext2_lookup(g_ec, "/k", &dir) == EXT2_OK);
    CHECK(ext2_create(g_ec, dir, "dst", X3_OLD, X3_OLD_LEN) == EXT2_OK);
    CHECK(ext2_find_entry(g_ec, dir, "dst", &d_ino, (u8 *)0) == EXT2_OK);
    {
        Ext2Inode di;
        CHECK(ext2_read_inode(g_ec, d_ino, &di) == EXT2_OK);
        di.links_count = 2;
        CHECK(ext2_write_inode(g_ec, d_ino, &di) == EXT2_OK);
    }
    CHECK(ext2_add_entry(g_ec, dir, "keep", d_ino, EXT2_FT_REG_FILE) == EXT2_OK);
    CHECK(ext2_create(g_ec, dir, "src", X3_NEW, X3_NEW_LEN) == EXT2_OK);
    CHECK(ext2_find_entry(g_ec, dir, "src", &s_ino, (u8 *)0) == EXT2_OK);
    CHECK(ext2_sync(g_ec) == EXT2_OK);

    CHECK(ext2_rename(g_ec, dir, "src", dir, "dst") == EXT2_OK);
    CHECK(raw_find_name(dir, "dst", &seen) == 1);
    CHECK(seen == s_ino);
    CHECK(raw_find_name(dir, "keep", &other) == 1);
    CHECK(other == d_ino);                      /* もう一つの名前はそのまま */
    CHECK(raw_inode_used(d_ino) == 1);          /* **解放しない** */
    CHECK(raw_links(d_ino) == 1);               /* 2 -> 1 に減った */
    CHECK(raw_links(s_ino) == 1);
    /* 旧内容がその名前から読めること (解放されて再利用されていない) */
    {
        u8 buf[32];
        int n = ext2_read_file(g_ec, d_ino, buf, sizeof(buf));
        CHECK(n == (int)X3_OLD_LEN);
        CHECK(n == (int)X3_OLD_LEN && kstrncmp((const char *)buf, X3_OLD,
                                               X3_OLD_LEN) == 0);
    }
    media_check(&r);
    CHECK_MEDIA(&r);
    disk_teardown();
}

static void stage_x3_replace(void)
{
    report("== 段 X3: 通常ファイル同士の置き換え rename (票 H2 §2-2) ==\n");
    fault_done();
    case_x3b(0, X3_MODE_BEFORE,  "前半セクタ / 媒体に届く前に失敗 -> 未公開");
    case_x3b(1, X3_MODE_BEFORE,  "後半セクタ / 媒体に届く前に失敗 -> 未公開");
    case_x3b(0, X3_MODE_LANDED,  "前半セクタ / 媒体に届いた後に失敗 -> 公開済み");
    case_x3b(1, X3_MODE_LANDED,  "後半セクタ / 媒体に届いた後に失敗 -> 公開済み");
    case_x3b(0, X3_MODE_UNKNOWN, "前半セクタ / 書き込みも読み直しも失敗 -> 不明");
    case_x3b(1, X3_MODE_UNKNOWN, "後半セクタ / 書き込みも読み直しも失敗 -> 不明");
    case_x3c();
    case_x3d();
    case_x3_links2();
}

/* ======================================================================== */
/*  入口                                                                    */
/* ======================================================================== */

static void run(void)
{
    report("=== 票 B8: 読み取り失敗を「不存在」にしない (vfs_open まで) ===\n");
    stage_a();
    stage_b();
    stage_c();

    report("\n");
    report_i(g_checks);
    report(" checks, ");
    report_i(g_failures);
    report(" failures\n");
    g_exit_code = g_failures ? 1 : 0;
}

/* -nostdlib の入口。プロセス開始時の esp は [argc][argv0][argv1]... を指すので
 * そのまま C へ渡す (ext2_write_io_host.c と同じ様式)。
 * argv[1] があれば、そのディレクトリへ RAM ディスクの像を書いて e2fsck と
 * 突き合わせる (票 B8 往復 5。test_b8_open.py が標準入出力で相手をする)。 */
void b8_start_c(long *sp);
__asm__(".text\n"
        ".globl _start\n"
        "_start:\n"
        "  movl %esp, %eax\n"
        "  andl $-16, %esp\n"
        "  pushl %eax\n"
        "  call b8_start_c\n"
        "  hlt\n");

/* "B8_PAIR_MAXN=<数>" を環境から拾う (libc が無いので envp を自前で歩く) */
static int env_int(const char **envp, const char *key, int dflt)
{
    int i, k, v;
    for (i = 0; envp[i]; i++) {
        const char *e = envp[i];
        for (k = 0; key[k] && e[k] == key[k]; k++) { }
        if (key[k] || e[k] != '=') continue;
        v = 0;
        for (k = k + 1; e[k] >= '0' && e[k] <= '9'; k++) v = v * 10 + (e[k] - '0');
        return v > 0 ? v : dflt;
    }
    return dflt;
}

void b8_start_c(long *sp)
{
    long argc = sp[0];
    const char **envp = (const char **)&sp[argc + 2];
    if (argc >= 2) g_dump_dir = (const char *)sp[2];
    g_pair_maxn = env_int(envp, "B8_PAIR_MAXN", PAIR_MAXN_DEFAULT);
    run();
    die(g_exit_code);
}
