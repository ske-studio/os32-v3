/* ========================================================================
 *  ext2_part_host.c — ext2 の区画探索と範囲だけへの format (票 TASK_HDD_INSTALL 段 1)
 *
 *  実行:   python3 -B tools/tests/test_hdd_stage1.py
 *  記録:   tools/tests/hdd_stage1_tdd.md
 *
 *  実物の fs/ext2_super.c / ext2_fmt.c / ext2_layout.c と drivers/pc98pt.c を
 *  そのまま取り込み、Device API・IDE・区画表の幾何 (bootinfo_part_geom) だけを
 *  贋物にする。見るのは:
 *    - ext2_find_partition が (開始, 長さ) を返し、見つからない / 読めない /
 *      幾何が無い / ディスクの外 / 旧配置のときは**失敗**する (1088 に化けない)
 *    - そのとき ext2_format は 1 セクタも書かない
 *    - 区画表の CHS は BIOS 幾何で LBA にする (IDENTIFY の 8/17 ではなく)
 *    - ext2_format_at は範囲の外を 1 セクタも書かず、範囲の検査で断るときは
 *      何も書かない。16,652 セクタ (Codex の例) で最終グループを落とす
 *    - マウントは FS が区画より大きければ断り、ブロック I/O は区画の外を断る
 *
 *  ext2_write_io_host.c と同じ様式 — ホスト ILP32 GNU89、-nostdlib。
 * ======================================================================== */

#include "ext2_priv.h"
#include "ide.h"
#include "kmalloc.h"
#include "../../drivers/pc98pt.h"
#include "../../drivers/pc98pt.c"
#include "ext2_layout.c"

/* ======================================================================== */
/*  libc の代わり (-nostdlib)                                               */
/* ======================================================================== */

static void die(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code));
    for (;;) { }
}

static u32 h_strlen(const char *s) { u32 n = 0; while (s[n]) n++; return n; }

static void out_raw(const void *p, u32 len)
{
    const u8 *b = (const u8 *)p;
    while (len > 0) {
        int n;
        u32 chunk = len > 65536u ? 65536u : len;
        __asm__ volatile("int $0x80" : "=a"(n) : "a"(4), "b"(1), "c"(b), "d"(chunk)
                         : "memory");
        if (n <= 0) die(3);
        b += n;
        len -= (u32)n;
    }
}

static void report(const char *text) { out_raw(text, h_strlen(text)); }

static void report_u(u32 v)
{
    char buf[16];
    int i = 15;
    buf[i] = '\0';
    if (v == 0) { buf[--i] = '0'; }
    while (v > 0) { buf[--i] = (char)('0' + (v % 10)); v /= 10; }
    report(&buf[i]);
}

static void fail(const char *what, int line)
{
    report("FAIL line ");
    report_u((u32)line);
    report(": ");
    report(what);
    report("\n");
    die(1);
}

#define CHECK(x) do { if (!(x)) fail(#x, __LINE__); } while (0)

/* ======================================================================== */
/*  kstring / kprintf / kmalloc の境界                                      */
/* ======================================================================== */

void *kmemcpy(void *dst, const void *src, u32 n)
{
    u8 *d = (u8 *)dst; const u8 *s = (const u8 *)src; u32 i;
    for (i = 0; i < n; i++) d[i] = s[i];
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

void *memcpy(void *dst, const void *src, u32 n) { return kmemcpy(dst, src, n); }
void *memset(void *dst, int val, u32 n) { return kmemset(dst, val, n); }
u32 strlen(const char *s) { return h_strlen(s); }
int strcmp(const char *a, const char *b) { return kstrcmp(a, b); }

void kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }

void *kzalloc(u32 size) { (void)size; return (void *)0; }
void kfree(void *p) { (void)p; }
void vfs_register_fs(VfsOps *ops) { (void)ops; }

/* ======================================================================== */
/*  RAM ディスク (64 シリンダ × 16/63 = 64,512 セクタ = 31.5MiB)             */
/* ======================================================================== */

#define DISK_TOTAL   64512u

static u8 g_disk[DISK_TOTAL * 512u];
static u32 g_rd, g_wr;
static u32 g_wr_min, g_wr_max;          /* 書いた LBA の範囲 */
static u32 g_fail_read_lba = 0xFFFFFFFFu;
static u32 g_ide_total = DISK_TOTAL;    /* IDENTIFY の総数 (ide_get_info) */
/* 配列より大きいディスクの真似 (m2 の頭打ち)。DISK_TOTAL 以上の LBA は
 * 書いても捨て (範囲だけ数える)、読むと 0 が返る */
static u32 g_virtual_total = DISK_TOTAL;

static void io_reset(void)
{
    g_rd = 0; g_wr = 0;
    g_wr_min = 0xFFFFFFFFu; g_wr_max = 0;
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
        if (cur >= g_virtual_total) return -1;
        if (cur == g_fail_read_lba) return -1;
        if (cur >= DISK_TOTAL) kmemset((u8 *)buf + i * 512, 0, 512);
        else kmemcpy((u8 *)buf + i * 512, g_disk + cur * 512u, 512);
        g_rd++;
    }
    return 0;
}

int dev_blk_write_lba(Device *dev, u32 lba, int count, const void *buf)
{
    int i;
    if (!dev) return -1;
    for (i = 0; i < count; i++) {
        u32 cur = lba + (u32)i;
        if (cur >= g_virtual_total) return -1;
        if (cur < DISK_TOTAL)
            kmemcpy(g_disk + cur * 512u, (const u8 *)buf + i * 512, 512);
        g_wr++;
        if (cur < g_wr_min) g_wr_min = cur;
        if (cur > g_wr_max) g_wr_max = cur;
    }
    return 0;
}

int ide_drive_present(int drive) { return (drive & 3) == 0; }

/* IDENTIFY は 8/17 と答える (区画表の幾何とは別。F4 の取り違えを見る) */
int ide_get_info(int drive, IdeInfo *info)
{
    if ((drive & 3) != 0) return IDE_ERR_NO_DRIVE;
    if (info) {
        kmemset(info, 0, sizeof(*info));
        info->cylinders = 474;
        info->heads = 8;
        info->sectors = 17;
        info->phys_sector_size = 512;
        info->total_sectors = g_ide_total;
    }
    return IDE_OK;
}

/* ATA の指定の方式で指せる上限 (ide.c の ide_range_ok の贋物)。既定は総数と同じ。
 * LBA28 の上限や現在の CHS の容量が総数より小さいドライブを g_ata_limit で作る */
static u32 g_ata_limit = DISK_TOTAL;

int ide_range_ok(int drive, u32 lba, u32 count)
{
    if ((drive & 3) != 0) return 0;
    if (count == 0) return 1;
    if (lba >= g_ata_limit) return 0;
    return count <= g_ata_limit - lba ? 1 : 0;
}

/* 区画表の幾何 (カーネルでは BIOS 幾何、無ければ IDENTIFY)。 */
static int g_geom_rc = 1;
static u16 g_geom_heads = 16, g_geom_spt = 63;

int bootinfo_part_geom(int ide_drive, u16 *heads, u16 *spt)
{
    if ((ide_drive & 3) != 0) return -1;
    if (g_geom_rc < 0) return g_geom_rc;
    if (heads) *heads = g_geom_heads;
    if (spt) *spt = g_geom_spt;
    return g_geom_rc;
}

/* ======================================================================== */
/*  実物のソース                                                            */
/* ======================================================================== */
#include "../../fs/ext2_super.c"
#include "../../fs/ext2_inode.c"
#include "../../fs/ext2_dir.c"
#include "../../fs/ext2_file.c"
#include "../../fs/ext2_fmt.c"

/* ======================================================================== */
/*  足場                                                                    */
/* ======================================================================== */

static Ext2Ctx g_ctx;

static void disk_reset(u16 heads, u16 spt)
{
    kmemset(g_disk, 0, sizeof(g_disk));
    kmemset(&g_hd0, 0, sizeof(g_hd0));
    kmemset(&g_ctx, 0, sizeof(g_ctx));
    g_hd0.name = "hd0";
    g_hd0.type = DEV_BLOCK;
    g_hd0.bus_type = DEV_BUS_IDE;
    g_hd0.sect_size = 512;
    g_hd0.total_sects = DISK_TOTAL;
    g_fail_read_lba = 0xFFFFFFFFu;
    g_ide_total = DISK_TOTAL;
    g_ata_limit = DISK_TOTAL;
    g_virtual_total = DISK_TOTAL;
    g_geom_rc = 1;
    g_geom_heads = heads;
    g_geom_spt = spt;
    io_reset();
}

static void pt_write(u32 start, u32 len, u16 heads, u16 spt)
{
    PC98PartEntry e;
    kmemset(g_disk + 512u, 0, 512);
    CHECK(pc98pt_make_os32(&e, start, len, heads, spt) == PC98PT_OK);
    CHECK(pc98pt_put(g_disk + 512u, 0, &e) == PC98PT_OK);
}

/* ======================================================================== */
/*  ケース                                                                  */
/* ======================================================================== */

/* 標準配置の区画を BIOS 幾何 (16/63) で見つける。IDENTIFY (8/17) ではない */
static void case_find_bios(void)
{
    u32 st = 0, ln = 0;

    disk_reset(16, 63);
    pt_write(2016, 1008u * 20u, 16, 63);
    CHECK(ext2_find_partition(0, &st, &ln) == EXT2_OK);
    CHECK(st == 2016 && ln == 20160);
    /* 幾何が IDENTIFY に落ちたとき (8/17) は同じ表が別の場所 = 272 を指す */
    g_geom_rc = 2; g_geom_heads = 8; g_geom_spt = 17;
    CHECK(ext2_find_partition(0, &st, &ln) == EXT2_OK);
    CHECK(st == 272);
    /* 8/17 の表 (NP21/W の NHD) */
    disk_reset(8, 17);
    pt_write(1632, 136u * 400u, 8, 17);
    CHECK(ext2_find_partition(0, &st, &ln) == EXT2_OK);
    CHECK(st == 1632 && ln == 54400);
}

/* 見つからない / 読めない / 幾何が無い / ディスクの外 / 旧配置 → 失敗。
 * format は 1 セクタも書かず、mount もしない */
static void case_find_fail(void)
{
    u32 st = 7, ln = 7;
    int step;

    for (step = 0; step < 6; step++) {
        int want;
        disk_reset(8, 17);
        switch (step) {
        case 0:   /* 区画表が空 (以前は 1088 に化けた) */
            want = EXT2_ERR_NOPART;
            break;
        case 1:   /* LBA 1 が読めない */
            pt_write(1632, 136u * 100u, 8, 17);
            g_fail_read_lba = 1;
            want = EXT2_ERR_IO;
            break;
        case 2:   /* 幾何が無い */
            pt_write(1632, 136u * 100u, 8, 17);
            g_geom_rc = -1;
            want = EXT2_ERR_NOPART;
            break;
        case 3:   /* 終わりがディスク (IDENTIFY の総数) の外 */
            pt_write(1632, 136u * 100u, 8, 17);
            g_ide_total = 1632u + 136u * 100u - 1u;
            want = EXT2_ERR_NOPART;
            break;
        case 4:   /* 旧配置 (cdinst / install が書いていたバイト列) */
            g_disk[512 + 0] = 0x80; g_disk[512 + 1] = 0xE2;
            g_disk[512 + 8] = 12;                            /* 旧: 開始 C = 12 */
            g_disk[512 + 10] = 16; g_disk[512 + 11] = 7;     /* 旧: 終了 S/H */
            g_disk[512 + 12] = 0xC2; g_disk[512 + 13] = 0x0B; /* 旧: 終了 C */
            want = EXT2_ERR_NOPART;
            break;
        default:  /* FAT の項目だけ */
            g_disk[512 + 0] = 0x80; g_disk[512 + 1] = 0xA1;
            want = EXT2_ERR_NOPART;
            break;
        }
        io_reset();
        CHECK(ext2_find_partition(0, &st, &ln) == want);
        CHECK(ext2_format(0, 16384) == want);
        CHECK(g_wr == 0);
        CHECK(ext2_mount(&g_ctx, 0) == want);
        CHECK(!g_ctx.mounted);
        CHECK(g_wr == 0);
    }
    /* 引数 */
    disk_reset(8, 17);
    CHECK(ext2_find_partition(0, (u32 *)0, &ln) == EXT2_ERR_INVAL);
    CHECK(ext2_find_partition(1, &st, &ln) == EXT2_ERR_IO);
}

/* ext2_format (従来の入口) は区画の長さで頭打ち */
static void case_format_clamp(void)
{
    disk_reset(8, 17);
    pt_write(1632, 136u * 100u, 8, 17);
    io_reset();
    CHECK(ext2_format(0, 0xFFFFFFF0u) == EXT2_OK);
    CHECK(g_wr_min >= 1632u && g_wr_max < 1632u + 13600u);
    CHECK(ext2_mount(&g_ctx, 0) == EXT2_OK);
    CHECK(g_ctx.base_lba == 1632u && g_ctx.part_len == 13600u);
    CHECK(g_ctx.sb_info.total_blocks == 6800u);
    CHECK(g_ctx.sb_info.total_blocks * 2u <= 13600u);
    ext2_unmount(&g_ctx);
}

/* Codex の 16,652 セクタ: 最終グループを落として 8,193 ブロック。範囲の外は書かない */
static void case_format_at_16652(void)
{
    u32 st, ln;

    disk_reset(16, 63);
    io_reset();
    CHECK(ext2_format_at(0, 2016, 16652) == EXT2_OK);
    CHECK(g_wr > 0);
    CHECK(g_wr_min == 2016u);
    CHECK(g_wr_max < 2016u + 16652u);
    /* 区画表は読まない・書かない */
    {
        u32 i;
        for (i = 0; i < 512u; i++) CHECK(g_disk[512u + i] == 0);
    }
    /* 区画表を書いてから通常のマウント (R3-1 の順) */
    pt_write(2016, 1008u * 17u, 16, 63);         /* 17136 >= 16652 */
    CHECK(ext2_find_partition(0, &st, &ln) == EXT2_OK);
    CHECK(ext2_mount(&g_ctx, 0) == EXT2_OK);
    CHECK(g_ctx.sb_info.total_blocks == 8193u);
    CHECK(g_ctx.num_groups == 1u);
    ext2_unmount(&g_ctx);
}

/* 範囲の検査で断るときは 1 セクタも書かない */
static void case_format_at_refuse(void)
{
    disk_reset(16, 63);
    io_reset();
    CHECK(ext2_format_at(0, 2016, 0) == EXT2_ERR_INVAL);
    CHECK(ext2_format_at(0, 17, 16384) == EXT2_ERR_INVAL);          /* ローダに掛かる */
    CHECK(ext2_format_at(0, 0, 16384) == EXT2_ERR_INVAL);           /* IPL */
    CHECK(ext2_format_at(0, DISK_TOTAL - 100u, 101u) == EXT2_ERR_INVAL); /* 1 セクタ外 */
    CHECK(ext2_format_at(0, DISK_TOTAL, 1u) == EXT2_ERR_INVAL);
    CHECK(ext2_format_at(0, 0xFFFFFFF0u, 0x20u) == EXT2_ERR_INVAL); /* 足すと 0 付近 */
    CHECK(ext2_format_at(0, 2016u, 0xFFFFFFFFu) == EXT2_ERR_INVAL);
    CHECK(ext2_format_at(1, 2016, 16384) == EXT2_ERR_IO);            /* ドライブが無い */
    g_ide_total = 0;                                                 /* 総数の申告が無い */
    CHECK(ext2_format_at(0, 2016, 16384) == EXT2_ERR_INVAL);
    g_ide_total = DISK_TOTAL;
    /* 総数の内側でも ATA の方式で指せない範囲 (LBA28 の上限・現在の CHS の容量) (C4) */
    g_ata_limit = 2016u + 16000u;
    CHECK(ext2_format_at(0, 2016, 16001) == EXT2_ERR_INVAL);
    g_ata_limit = DISK_TOTAL;
    /* ATA の方式では指せても IDENTIFY の総数の外 */
    g_ide_total = DISK_TOTAL - 1000u;
    CHECK(ext2_format_at(0, DISK_TOTAL - 20160u, 20160u) == EXT2_ERR_INVAL);
    g_ide_total = DISK_TOTAL;
    CHECK(ext2_format_at(0, 2016, 120) == EXT2_ERR_NOSPC);           /* 64 ブロック未満 */
    CHECK(g_wr == 0);
    /* ちょうどディスクの終わりまでは通る (範囲の中だけを書く) */
    CHECK(ext2_format_at(0, DISK_TOTAL - 20160u, 20160u) == EXT2_OK);
    CHECK(g_wr_min == DISK_TOTAL - 20160u);
    CHECK(g_wr_max < DISK_TOTAL);
}

/* FS が区画より大きい (区画表と中身の食い違い) → マウントしない。
 * マウント後のブロック I/O は区画の外を断る (デバイスを触らない) */
static void case_mount_bounds(void)
{
    u32 rd0;

    disk_reset(8, 17);
    CHECK(ext2_format_at(0, 1632, 136u * 200u) == EXT2_OK);
    pt_write(1632, 136u * 100u, 8, 17);          /* 区画を半分に */
    CHECK(ext2_mount(&g_ctx, 0) == EXT2_ERR_NOPART);
    CHECK(!g_ctx.mounted);

    pt_write(1632, 136u * 200u, 8, 17);
    CHECK(ext2_mount(&g_ctx, 0) == EXT2_OK);
    CHECK(g_ctx.part_len == 27200u);
    rd0 = g_rd;
    CHECK(ext2_read_data_block(&g_ctx, 13599u, ext2_g_aux) == 0);   /* 最後のブロック */
    CHECK(g_rd == rd0 + 2u);
    rd0 = g_rd;
    CHECK(ext2_read_data_block(&g_ctx, 13600u, ext2_g_aux) != 0);   /* 区画の外 */
    CHECK(ext2_write_data_block(&g_ctx, 13600u, ext2_g_aux) != 0);
    CHECK(ext2_read_data_block(&g_ctx, 0x80000000u, ext2_g_aux) != 0); /* ×2 で桁あふれ */
    CHECK(ext2_read_sector(&g_ctx, 13600u, 0, ext2_g_aux) != 0);
    CHECK(ext2_write_sector(&g_ctx, 13599u, 2, ext2_g_aux) != 0);    /* sect は 0/1 */
    CHECK(g_rd == rd0);
    ext2_unmount(&g_ctx);
}

/* ext2_format は BIOS 幾何で区画を見つけたときだけ書く (m3)。
 * 区画が 32 グループより大きければ上限の大きさへ頭打ち (m2) */
static void case_format_geom(void)
{
    disk_reset(8, 17);
    pt_write(1632, 136u * 100u, 8, 17);
    g_geom_rc = 2;                                /* IDENTIFY に落ちた */
    io_reset();
    CHECK(ext2_format(0, 13600) == EXT2_ERR_INVAL);
    CHECK(g_wr == 0);
    /* 読むだけ (マウント) は IDENTIFY でも通る */
    g_geom_rc = 1;
    CHECK(ext2_format(0, 13600) == EXT2_OK);
    g_geom_rc = 2;
    CHECK(ext2_mount(&g_ctx, 0) == EXT2_OK);
    ext2_unmount(&g_ctx);
    CHECK(EXT2L_MAX_SECTORS(EXT2_MAX_GROUPS) == 524290u);

    /* 300MiB の区画 (33 グループ分) → ext2_format は 32 グループへ頭打ちして作る */
    disk_reset(16, 63);
    g_virtual_total = 2016u + 1008u * 610u;       /* 616,896 セクタ */
    g_ide_total = g_virtual_total;
    g_ata_limit = g_virtual_total;
    pt_write(2016, 1008u * 608u, 16, 63);         /* 612,864 セクタ = 299MiB */
    io_reset();
    CHECK(ext2_format(0, 0xFFFFFFF0u) == EXT2_OK);
    CHECK(g_wr_min == 2016u && g_wr_max < 2016u + 524290u);
    CHECK(ext2_mount(&g_ctx, 0) == EXT2_OK);
    CHECK(g_ctx.num_groups == 32u && g_ctx.sb_info.total_blocks == 262145u);
    ext2_unmount(&g_ctx);
    /* 頭打ちの後の範囲 (524,290 セクタ) が ATA の方式で指せなければ書かない
     * (ext2_format の経路も ext2_format_range の入口で照合、Codex ラリー 2 の 3) */
    g_ata_limit = 2016u + 400000u;
    io_reset();
    CHECK(ext2_format(0, 0xFFFFFFF0u) == EXT2_ERR_INVAL);
    CHECK(g_wr == 0);
    g_ata_limit = g_virtual_total;
    /* format_at は大きすぎる範囲を頭打ちにせず断る (hdprep は上限の内側で計画する) */
    io_reset();
    CHECK(ext2_format_at(0, 2016, 1008u * 608u) == EXT2_ERR_NOSPC);
    CHECK(g_wr == 0);
}

/* 旧配置の区画表でマウントすると NOPART (案内は kprintf、ここでは判定だけ) */
static void case_legacy_hint(void)
{
    disk_reset(8, 17);
    g_disk[512 + 0] = 0x80; g_disk[512 + 1] = 0xE2;
    g_disk[512 + 8] = 12;
    g_disk[512 + 10] = 16; g_disk[512 + 11] = 7;
    g_disk[512 + 12] = 400u & 0xFFu; g_disk[512 + 13] = (u8)(400u >> 8);
    CHECK(pc98pt_os32_is_legacy(g_disk + 512, 8, 17, DISK_TOTAL) == 1);
    CHECK(ext2_mount(&g_ctx, 0) == EXT2_ERR_NOPART);
    /* 標準配置の表は legacy ではない */
    pt_write(1632, 136u * 100u, 8, 17);
    CHECK(pc98pt_os32_is_legacy(g_disk + 512, 8, 17, DISK_TOTAL) == 0);
    /* 空の表・FAT だけも legacy ではない */
    kmemset(g_disk + 512, 0, 512);
    CHECK(pc98pt_os32_is_legacy(g_disk + 512, 8, 17, DISK_TOTAL) == 0);
    g_disk[512 + 0] = 0x80; g_disk[512 + 1] = 0xA1;
    CHECK(pc98pt_os32_is_legacy(g_disk + 512, 8, 17, DISK_TOTAL) == 0);
}

/* 像の書き出し (test_hdd_stage1.py が e2fsck -fn にかける) */
static void dump_format_at(u32 start, u32 len)
{
    disk_reset(16, 63);
    if (ext2_format_at(0, start, len) != EXT2_OK) die(4);
    out_raw(g_disk + start * 512u, len * 512u);
}

static int streq(const char *a, const char *b) { return kstrcmp(a, b) == 0; }

static u32 atou(const char *s)
{
    u32 v = 0;
    while (*s >= '0' && *s <= '9') v = v * 10u + (u32)(*s++ - '0');
    return v;
}

int os32_main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "";
    if (streq(c, "find_bios")) case_find_bios();
    else if (streq(c, "find_fail")) case_find_fail();
    else if (streq(c, "format_clamp")) case_format_clamp();
    else if (streq(c, "format_at_16652")) case_format_at_16652();
    else if (streq(c, "format_at_refuse")) case_format_at_refuse();
    else if (streq(c, "mount_bounds")) case_mount_bounds();
    else if (streq(c, "format_geom")) case_format_geom();
    else if (streq(c, "legacy_hint")) case_legacy_hint();
    else if (streq(c, "dump") && argc == 4) { dump_format_at(atou(argv[2]), atou(argv[3])); return 0; }
    else { report("unknown case\n"); return 2; }
    report("ext2_part: PASS (");
    report(c);
    report(")\n");
    return 0;
}

/* -nostdlib の入口。プロセス開始時の esp は [argc][argv0][argv1]... を
 * 指しているので、そのまま C へ渡す (ext2_write_io_host.c と同じ)。 */
void start_c(long *sp);

__asm__(".text\n"
        ".globl _start\n"
        "_start:\n"
        "  movl %esp, %eax\n"
        "  andl $-16, %esp\n"
        "  pushl %eax\n"
        "  call start_c\n"
        "  hlt\n");

void start_c(long *sp)
{
    int argc = (int)sp[0];
    char **argv = (char **)&sp[1];
    die(os32_main(argc, argv));
}
