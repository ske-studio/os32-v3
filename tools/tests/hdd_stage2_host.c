/* ========================================================================
 *  hdd_stage2_host.c — インストーラの判定 (純粋関数) の試験 (票 TASK_HDD_INSTALL 段 2)
 *
 *  実行:   python3 -B tools/tests/test_hdd_stage2.py
 *  記録:   tools/tests/hdd_stage2_tdd.md
 *
 *  実物 userland/system/inst_disk.c を、drivers/pc98pt.c・
 *  userland/shell/hdprep_plan.c・fs/ext2_layout.c と別の翻訳単位で組む
 *  (ホスト LP64、libc あり)。見るもの:
 *    - モードの判定 (空・再作成・旧配置・未知・2 項目・開始違い・55AA・壊れ)
 *      を 8/17 と 16/63 で
 *    - 区画表と IPL の幾何が一致する (inst_build_pt / inst_patch_ipl)
 *    - 媒体の大きさの境界 (IPL 512・ローダ 8192・vmkernel 508KiB)
 *    - 容量の見積もり (間接ブロック、ブロック / inode の不足、配置が無い)
 *  `room <sectors>` は空き (ブロック・inode) を出す — 試験は実物の
 *  ext2_format_at の像の dumpe2fs と突き合わせる。
 * ======================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drivers/pc98pt.h"
#include "userland/system/inst_disk.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", \
    __func__, __LINE__, #c); exit(1); } } while (0)

static unsigned char lba0[512], lba1[512];

static void clear(void) { memset(lba0, 0, 512); memset(lba1, 0, 512); }

static void put_std(int idx, unsigned long start, unsigned long len,
                    unsigned long h, unsigned long s)
{
    PC98PartEntry e;
    CHECK(pc98pt_make_os32(&e, start, len, h, s) == PC98PT_OK);
    CHECK(pc98pt_put(lba1, idx, &e) == PC98PT_OK);
}

/* 2026-09-23 までの cdinst / install の書き方 (8/17 固定、シリンダ 12 から) */
static void put_legacy(unsigned long scyl, unsigned long ecyl)
{
    unsigned char *p = lba1;
    int i;
    p[0] = 0x80; p[1] = 0xE2; p[6] = 0; p[7] = 0;
    p[8] = (unsigned char)scyl; p[9] = (unsigned char)(scyl >> 8);
    p[10] = 16; p[11] = 7;
    p[12] = (unsigned char)ecyl; p[13] = (unsigned char)(ecyl >> 8);
    memcpy(p + 16, "OS32", 4);
    for (i = 20; i < 32; i++) p[i] = ' ';
}

static int cls(unsigned long h, unsigned long s, unsigned long total,
               unsigned long expect, int *mode)
{
    *mode = 0;
    return inst_classify(lba0, lba1, h, s, total, expect, mode);
}

static void classify_geom(unsigned long h, unsigned long s, unsigned long total,
                          unsigned long expect)
{
    int mode;
    unsigned long cyl = h * s;

    /* 空 */
    clear();
    CHECK(cls(h, s, total, expect, &mode) == 0 && mode == INST_MODE_EMPTY);
    /* 空だが LBA 0 に 55AA */
    lba0[510] = 0x55; lba0[511] = 0xAA;
    CHECK(cls(h, s, total, expect, &mode) == HDPREP_E_MBR_SIG);
    /* 55 だけ / AA だけは空のまま */
    lba0[511] = 0;
    CHECK(cls(h, s, total, expect, &mode) == 0 && mode == INST_MODE_EMPTY);

    /* 再作成: 標準配置の OS32 1 項目、開始 = 期待値 (長さは問わない) */
    clear();
    put_std(0, expect, cyl * 10, h, s);
    CHECK(cls(h, s, total, expect, &mode) == 0 && mode == INST_MODE_RECREATE);
    lba0[510] = 0x55; lba0[511] = 0xAA;            /* 55AA があっても再作成 */
    CHECK(cls(h, s, total, expect, &mode) == 0 && mode == INST_MODE_RECREATE);
    /* 表の途中の 1 項目 */
    clear();
    put_std(7, expect, cyl * 10, h, s);
    CHECK(cls(h, s, total, expect, &mode) == 0 && mode == INST_MODE_RECREATE);
    /* 名前の後ろが NUL 埋めでも OS32 */
    memset(lba1 + 7 * 32 + 20, 0, 12);
    CHECK(cls(h, s, total, expect, &mode) == 0 && mode == INST_MODE_RECREATE);
    /* 開始が 1 シリンダ後ろ / 前 */
    clear();
    put_std(0, expect + cyl, cyl * 10, h, s);
    CHECK(cls(h, s, total, expect, &mode) == INST_E_START);
    clear();
    put_std(0, expect - cyl, cyl * 10, h, s);
    CHECK(cls(h, s, total, expect, &mode) == INST_E_START);
    /* 名前が違う (OS32X / os32 / 空) */
    clear();
    put_std(0, expect, cyl * 10, h, s);
    lba1[16 + 4] = 'X';
    CHECK(cls(h, s, total, expect, &mode) == INST_E_FOREIGN);
    clear();
    put_std(0, expect, cyl * 10, h, s);
    lba1[16] = 'o';
    CHECK(cls(h, s, total, expect, &mode) == INST_E_FOREIGN);
    /* sid が違う (名前は OS32) */
    clear();
    put_std(0, expect, cyl * 10, h, s);
    lba1[1] = 0xA1;
    CHECK(cls(h, s, total, expect, &mode) == INST_E_FOREIGN);
    /* 2 項目 (両方 OS32 でも) */
    clear();
    put_std(0, expect, cyl * 10, h, s);
    put_std(1, expect + cyl * 10, cyl * 10, h, s);
    CHECK(cls(h, s, total, expect, &mode) == INST_E_MULTI);
    /* mid だけ立った項目も数える */
    clear();
    put_std(0, expect, cyl * 10, h, s);
    lba1[5 * 32] = 0x80;
    CHECK(cls(h, s, total, expect, &mode) == INST_E_MULTI);
    /* ディスクの外に出る項目は壊れ */
    clear();
    put_std(0, expect, cyl * 10, h, s);
    CHECK(cls(h, s, expect + cyl * 5, expect, &mode) == INST_E_BROKEN);
    /* 壊れ (どちらの配置でも範囲にならない) */
    clear();
    lba1[0] = 0x80; lba1[1] = 0xE2; lba1[8] = 200;
    memcpy(lba1 + 16, "OS32            ", 16);
    CHECK(cls(h, s, total, expect, &mode) == INST_E_BROKEN);
}

static void case_classify817(void)
{
    int mode;
    classify_geom(8, 17, 409600, 1632);
    /* 旧配置 (シリンダ 12 = LBA 1632) は作り直す */
    clear();
    put_legacy(12, 409600 / 136 - 1);
    lba0[510] = 0x55; lba0[511] = 0xAA;
    CHECK(cls(8, 17, 409600, 1632, &mode) == 0 && mode == INST_MODE_RECREATE_OLD);
    /* 旧配置でも開始シリンダ 13 は断る */
    clear();
    put_legacy(13, 409600 / 136 - 1);
    CHECK(cls(8, 17, 409600, 1632, &mode) == INST_E_START);
    /* 旧配置で終わりがディスクの外 → 壊れ */
    clear();
    put_legacy(12, 409600 / 136 + 5);
    CHECK(cls(8, 17, 409600, 1632, &mode) == INST_E_BROKEN);
}

static void case_classify1663(void)
{
    int mode;
    classify_geom(16, 63, 16514063, 2016);
    /* 旧配置のシリンダ 12 は 16/63 で LBA 12,096 ≠ 2016 → 断る */
    clear();
    put_legacy(12, 2000);
    CHECK(cls(16, 63, 16514063, 2016, &mode) == INST_E_START);
    /* 8/17 で作った標準配置の表 (シリンダ 12) を 16/63 で読むと開始 12,096 */
    clear();
    put_std(0, 1632, 136 * 100, 8, 17);
    CHECK(cls(16, 63, 16514063, 2016, &mode) == INST_E_START);
}

static void case_paths(void)
{
    char buf[128];
    int i;

    CHECK(inst_check_path("/a") == 0);
    CHECK(inst_check_path("/boot/vmkernel.lz4") == 0);
    CHECK(inst_check_path("/a/.b/..c/...") == 0);          /* 名前の一部の '.' は可 */
    CHECK(inst_check_path("") == INST_E_PATH);
    CHECK(inst_check_path("/") == INST_E_PATH);
    CHECK(inst_check_path("a/b") == INST_E_PATH);
    CHECK(inst_check_path("ab") == INST_E_PATH);
    CHECK(inst_check_path("//a") == INST_E_PATH);
    CHECK(inst_check_path("/a//b") == INST_E_PATH);
    CHECK(inst_check_path("/a/") == INST_E_PATH);
    CHECK(inst_check_path("/.") == INST_E_PATH);
    CHECK(inst_check_path("/..") == INST_E_PATH);
    CHECK(inst_check_path("/a/./b") == INST_E_PATH);
    CHECK(inst_check_path("/../hd1/x") == INST_E_PATH);
    CHECK(inst_check_path("/a/b/..") == INST_E_PATH);
    CHECK(inst_check_path(NULL) == INST_E_PATH);
    /* 深さ: "/hd0" を足して 32 要素まで */
    for (i = 0; i < 31; i++) { buf[i * 2] = '/'; buf[i * 2 + 1] = 'x'; }
    buf[62] = '\0';
    CHECK(inst_check_path(buf) == 0);
    buf[62] = '/'; buf[63] = 'y'; buf[64] = '\0';
    CHECK(inst_check_path(buf) == INST_E_DEPTH);
}

static void case_bootfiles(void)
{
    CHECK(inst_check_boot_files(512, 8192, 508UL * 1024UL) == 0);
    CHECK(inst_check_boot_files(1, 1, 1) == 0);
    CHECK(inst_check_boot_files(0, 8192, 1000) == INST_E_IPL_SIZE);
    CHECK(inst_check_boot_files(513, 8192, 1000) == INST_E_IPL_SIZE);
    CHECK(inst_check_boot_files(512, 0, 1000) == INST_E_LOADER_SIZE);
    CHECK(inst_check_boot_files(512, 8193, 1000) == INST_E_LOADER_SIZE);
    CHECK(inst_check_boot_files(512, 8192, 0) == INST_E_KERNEL_SIZE);
    CHECK(inst_check_boot_files(512, 8192, 508UL * 1024UL + 1UL) == INST_E_KERNEL_SIZE);
}

static void case_blocks(void)
{
    CHECK(inst_file_blocks(0) == 0);
    CHECK(inst_file_blocks(1) == 1);
    CHECK(inst_file_blocks(1024) == 1);
    CHECK(inst_file_blocks(1025) == 2);
    CHECK(inst_file_blocks(12UL * 1024UL) == 12);
    CHECK(inst_file_blocks(12UL * 1024UL + 1UL) == 14);          /* 13 + 単一間接 */
    CHECK(inst_file_blocks(268UL * 1024UL) == 269);              /* 12 + 256 + 単一間接 */
    CHECK(inst_file_blocks(269UL * 1024UL) == 272);              /* + 二重間接 1 + 表 1 */
    CHECK(inst_file_blocks(508UL * 1024UL) == 508 + 1 + 1 + 1);  /* vmkernel の上限 */
    CHECK(inst_file_blocks(524UL * 1024UL) == 524 + 1 + 1 + 1);  /* 二重間接の表 1 枚ちょうど */
    CHECK(inst_file_blocks(525UL * 1024UL) == 525 + 1 + 1 + 2);
}

static void case_space(void)
{
    InstNeed n;
    InstRoom r;

    inst_need_init(&n);
    CHECK(inst_check_space(407864, &n, &r) == 0);
    CHECK(r.free_blocks > 190000 && r.free_blocks < 203932);
    CHECK(r.need_blocks == 1 + INST_SPACE_MARGIN_BLOCKS);
    /* ちょうど収まる / 1 ブロック足りない */
    inst_need_init(&n);
    n.blocks = r.free_blocks - 1 - INST_SPACE_MARGIN_BLOCKS;
    CHECK(inst_check_space(407864, &n, &r) == 0);
    n.blocks++;
    CHECK(inst_check_space(407864, &n, &r) == INST_E_SPACE);
    /* inode が足りない (0 バイトのファイルばかり)。自動で作る親ディレクトリの
     * 余白 INST_SPACE_MARGIN_INODES を残す */
    inst_need_init(&n);
    n.files = r.free_inodes - INST_SPACE_MARGIN_INODES + 1;
    CHECK(inst_check_space(407864, &n, &r) == INST_E_INODES);
    n.files--;
    CHECK(inst_check_space(407864, &n, &r) == 0);
    CHECK(r.need_inodes == r.free_inodes);
    /* 失敗でも room は 0 で埋まる */
    r.free_blocks = r.free_inodes = 12345;
    CHECK(inst_check_space(100, &n, &r) == INST_E_LAYOUT);
    CHECK(r.free_blocks == 0 && r.free_inodes == 0 && r.need_blocks == 0);
    /* 1 ファイルの上限 (二重間接まで) ちょうどは通り、+1 は断る */
    CHECK(INST_EXT2_MAX_FILE == 67383296UL);
    inst_need_init(&n);
    inst_need_file(&n, INST_EXT2_MAX_FILE);
    CHECK(inst_check_space(524160, &n, &r) == 0);
    inst_need_file(&n, INST_EXT2_MAX_FILE + 1UL);
    CHECK(inst_check_space(524160, &n, &r) == INST_E_FILE_SIZE);
    /* 配置が成り立たない大きさ */
    inst_need_init(&n);
    CHECK(inst_check_space(100, &n, &r) == INST_E_LAYOUT);
    /* 見積もりは多めに数える: ファイル 1 本 + ディレクトリ 1 つ */
    inst_need_init(&n);
    inst_need_file(&n, 470000);
    inst_need_dir(&n);
    CHECK(n.files == 1 && n.dirs == 1 && n.blocks == inst_file_blocks(470000) + 1);
    CHECK(inst_check_space(524160, &n, &r) == 0);
    CHECK(r.need_inodes == 2 + INST_SPACE_MARGIN_INODES);
}

/* 区画表と IPL の幾何が一致する (8/17・16/63)、計画は hdprep と同じ */
static void one_plan(unsigned long h, unsigned long s, unsigned long bcyl,
                     unsigned long total, unsigned long want_start, unsigned long want_len)
{
    HdprepGeom g;
    HdprepPlan p;
    unsigned char pt[512], ipl[512];
    unsigned long st, ln;
    int idx, i;

    memset(&g, 0, sizeof(g));
    g.ata_present = 1; g.addr_mode = HDPREP_AMODE_LBA28; g.ata_total = total;
    g.bios_queried = 1; g.bios_valid = 1; g.bios_cyl = bcyl;
    g.bios_heads = h; g.bios_spt = s; g.bios_seclen = 512;
    CHECK(hdprep_plan(&g, INST_PART_MB, &p) == HDPREP_OK);
    CHECK(p.start == want_start && p.len == want_len);
    memset(pt, 0xCC, sizeof(pt));
    CHECK(inst_build_pt(pt, &p) == 0);
    CHECK(pc98pt_count_used(pt) == 1);
    CHECK(pc98pt_find_os32(pt, h, s, total, &idx, &st, &ln) == PC98PT_OK);
    CHECK(idx == 0 && st == p.start && ln == p.len);
    for (i = 32; i < 512; i++) CHECK(pt[i] == 0);     /* 残りの項目は空 */
    memset(ipl, 0x11, sizeof(ipl));
    inst_patch_ipl(ipl, p.heads, p.spt);
    CHECK(ipl[INST_IPL_OFF_HEADS] == h && ipl[INST_IPL_OFF_SPT] == s);
    CHECK(ipl[510] == 0x55 && ipl[511] == 0xAA);
    for (i = 0; i < 510; i++)
        if (i != INST_IPL_OFF_HEADS && i != INST_IPL_OFF_SPT) CHECK(ipl[i] == 0x11);
    /* 旧 install が IPL に書いた IDENTIFY の幾何ではなく、区画表と同じ BIOS 幾何 */
    CHECK(p.heads == h && p.spt == s);
}

static void case_iplpt(void)
{
    one_plan(8, 17, 3011, 409600, 1632, 407864);
    one_plan(16, 63, 16382, 16514063, 2016, 524160);
    one_plan(8, 17, 65535, 16514063, 1632, 524280);         /* 256MiB を 136 で切り下げ */
}

int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "room")) {
        InstNeed n;
        InstRoom r;
        inst_need_init(&n);
        (void)inst_check_space(strtoul(argv[2], NULL, 10), &n, &r);
        printf("%lu %lu\n", r.free_blocks, r.free_inodes);
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "consts")) {
        printf("%lu %lu %lu %d %d\n", INST_KERNEL_MAX, INST_LOADER_MAX,
               INST_EXT2_MAX_GROUPS, INST_IPL_OFF_HEADS, INST_IPL_OFF_SPT);
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "pathok")) {    /* 実物の PKG のパスを並べて渡す */
        int i;
        for (i = 2; i < argc; i++)
            if (inst_check_path(argv[i]) != 0) { printf("BAD %s\n", argv[i]); return 1; }
        printf("PASS pathok (%d paths)\n", argc - 2);
        return 0;
    }
    if (argc != 2) return 2;
    if (!strcmp(argv[1], "classify817")) case_classify817();
    else if (!strcmp(argv[1], "classify1663")) case_classify1663();
    else if (!strcmp(argv[1], "bootfiles")) case_bootfiles();
    else if (!strcmp(argv[1], "paths")) case_paths();
    else if (!strcmp(argv[1], "blocks")) case_blocks();
    else if (!strcmp(argv[1], "space")) case_space();
    else if (!strcmp(argv[1], "iplpt")) case_iplpt();
    else return 2;
    printf("PASS %s\n", argv[1]);
    return 0;
}
