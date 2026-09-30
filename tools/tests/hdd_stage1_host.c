/* ======================================================================== */
/*  HDD_STAGE1_HOST.C — 票 TASK_HDD_INSTALL 段 1 の純粋関数をそのまま回す    */
/*                                                                          */
/*  実物を 1 行も写さずに #include する:                                    */
/*    drivers/pc98pt.c          区画表の共有部 (標準配置)                   */
/*    drivers/ide_addr.c        ATA の指定の方式 (LBA28 / 現在 / 既定) と範囲 */
/*    fs/ext2_layout.c          format の大きさの固定点 (N7)                 */
/*    userland/shell/hdprep_plan.c  hdprep の断る条件と計画                  */
/*  記録: tools/tests/hdd_stage1_tdd.md                                     */
/*  票  : docs/archive/realhw_v21/TASK_HDD_INSTALL.md 段 1 / §1-v3                */
/*                                                                          */
/*  ホストは 64 ビット (u32 = unsigned long = 64 bit)。試験対象は 32 ビットの */
/*  桁あふれに頼らない書き方をしているので、同じ答えになる。期待値は式では   */
/*  なく数で書く。                                                          */
/* ======================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include "../../drivers/pc98pt.c"
#include "../../drivers/ide_addr.c"
#include "../../fs/ext2_layout.c"
#include "../../userland/shell/hdprep_plan.c"

static int failed;
#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #x); failed++; \
} } while (0)

/* ======================================================================== */
/*  区画表                                                                  */
/* ======================================================================== */

/* 構造体 PC98PartEntry (fatfs と共有) の並びが OFF_* と一致する */
static void case_pt_offsets(void)
{
    CHECK(sizeof(PC98PartEntry) == 32);
    CHECK(offsetof(PC98PartEntry, bootable)     == PC98PT_OFF_MID);
    CHECK(offsetof(PC98PartEntry, sys_id)       == PC98PT_OFF_SID);
    CHECK(offsetof(PC98PartEntry, ipl_sector)   == PC98PT_OFF_IPL_SECT);
    CHECK(offsetof(PC98PartEntry, ipl_head)     == PC98PT_OFF_IPL_HEAD);
    CHECK(offsetof(PC98PartEntry, ipl_cyl)      == PC98PT_OFF_IPL_CYL);
    CHECK(offsetof(PC98PartEntry, start_sector) == PC98PT_OFF_SSECT);
    CHECK(offsetof(PC98PartEntry, start_head)   == PC98PT_OFF_SHEAD);
    CHECK(offsetof(PC98PartEntry, start_cyl)    == PC98PT_OFF_SCYL);
    CHECK(offsetof(PC98PartEntry, end_sector)   == PC98PT_OFF_ESECT);
    CHECK(offsetof(PC98PartEntry, end_head)     == PC98PT_OFF_EHEAD);
    CHECK(offsetof(PC98PartEntry, end_cyl)      == PC98PT_OFF_ECYL);
    CHECK(offsetof(PC98PartEntry, name)         == PC98PT_OFF_NAME);
    /* 標準の数値 (FreeBSD diskpc98.h の dp_ssect = +8 / dp_scyl = +10) */
    CHECK(PC98PT_OFF_SSECT == 8 && PC98PT_OFF_SHEAD == 9 && PC98PT_OFF_SCYL == 10);
    CHECK(PC98PT_OFF_ESECT == 12 && PC98PT_OFF_EHEAD == 13 && PC98PT_OFF_ECYL == 14);
}

/* 8/17: NHD の 1632 から 3011 シリンダの終わりまで (実 NHD と同じ値) */
static void case_pt_817(void)
{
    unsigned char sect[512];
    PC98PartEntry e;
    unsigned long st = 0, ln = 0;
    int idx = -1;

    memset(sect, 0, sizeof(sect));
    CHECK(pc98pt_make_os32(&e, 1632, 407864, 8, 17) == PC98PT_OK);
    CHECK(pc98pt_put(sect, 0, &e) == PC98PT_OK);
    /* バイト列 (期待値は手で書いた数) */
    CHECK(sect[0] == 0x80 && sect[1] == 0xE2);
    CHECK(sect[4] == 0 && sect[5] == 0 && sect[6] == 12 && sect[7] == 0);   /* IPL 12/0/0 */
    CHECK(sect[8] == 0 && sect[9] == 0 && sect[10] == 12 && sect[11] == 0); /* 開始 12/0/0 */
    CHECK(sect[12] == 16 && sect[13] == 7);                                /* 終了 S/H */
    CHECK(sect[14] == 0xC2 && sect[15] == 0x0B);                           /* 終了 C = 3010 */
    CHECK(memcmp(sect + 16, "OS32            ", 16) == 0);
    CHECK(pc98pt_count_used(sect) == 1);
    CHECK(pc98pt_find_os32(sect, 8, 17, 409496, &idx, &st, &ln) == PC98PT_OK);
    CHECK(idx == 0 && st == 1632 && ln == 407864);
    /* 標準配置で読める表は「旧配置」と言わない (旧として読んでもディスクの内側に
     * 収まってしまう大きさで確かめる) */
    CHECK(pc98pt_os32_is_legacy(sect, 8, 17, 409496) == 0);
    /* ディスクが 1 セクタ短いと外 */
    CHECK(pc98pt_find_os32(sect, 8, 17, 409495, &idx, &st, &ln) == PC98PT_ERR_RANGE);
}

/* 16/63: 開始は 1632 以上の最初のシリンダ境界 = 2016 (段 2-9) */
static void case_pt_1663(void)
{
    unsigned char sect[512];
    PC98PartEntry e, back;
    unsigned long st = 0, ln = 0;

    memset(sect, 0, sizeof(sect));
    CHECK(pc98pt_make_os32(&e, 2016, 256 * 1008, 16, 63) == PC98PT_OK);
    CHECK(pc98pt_put(sect, 3, &e) == PC98PT_OK);
    CHECK(pc98pt_get(sect, 3, &back) == PC98PT_OK);
    CHECK(memcmp(&e, &back, sizeof(e)) == 0);
    CHECK(sect[3 * 32 + 10] == 2 && sect[3 * 32 + 12] == 62 && sect[3 * 32 + 13] == 15);
    CHECK(sect[3 * 32 + 14] == 1 && sect[3 * 32 + 15] == 1);   /* 終了 C = 257 */
    CHECK(pc98pt_find_os32(sect, 16, 63, 16514063, 0, &st, &ln) == PC98PT_OK);
    CHECK(st == 2016 && ln == 258048);
    /* 同じ表を 8/17 で読むと別の場所になる (幾何を取り違えない試験の前提) */
    CHECK(pc98pt_find_os32(sect, 8, 17, 16514063, 0, &st, &ln) == PC98PT_OK);
    CHECK(st == 272);
}

static void case_pt_reject(void)
{
    unsigned char sect[512];
    PC98PartEntry e;
    unsigned long st, ln;

    /* 書き手: 境界に無い / 長さ 0 / 幾何 0 / シリンダ 16 ビット超 */
    CHECK(pc98pt_make_os32(&e, 1633, 136, 8, 17) == PC98PT_ERR_ALIGN);
    CHECK(pc98pt_make_os32(&e, 1632, 137, 8, 17) == PC98PT_ERR_ALIGN);
    CHECK(pc98pt_make_os32(&e, 1632, 0, 8, 17) == PC98PT_ERR_RANGE);
    CHECK(pc98pt_make_os32(&e, 1632, 136, 0, 17) == PC98PT_ERR_GEOM);
    CHECK(pc98pt_make_os32(&e, 1632, 136, 8, 0) == PC98PT_ERR_GEOM);
    CHECK(pc98pt_make_os32(&e, 1632, 136, 256, 17) == PC98PT_ERR_GEOM);
    /* F3: 8GB を 8/17 で覆うと終了シリンダ 121,425 (旧 install は下位 16 ビットを書いた) */
    CHECK(pc98pt_make_os32(&e, 1632, 121414UL * 136UL, 8, 17) == PC98PT_ERR_CYL);
    CHECK(pc98pt_make_os32(&e, 1632, (65536UL - 12UL) * 136UL, 8, 17) == PC98PT_OK);
    CHECK(pc98pt_make_os32(&e, 1632, (65537UL - 12UL) * 136UL, 8, 17) == PC98PT_ERR_CYL);

    /* 読み手: 項目が無い */
    memset(sect, 0, sizeof(sect));
    CHECK(pc98pt_find_os32(sect, 8, 17, 409496, 0, &st, &ln) == PC98PT_ERR_NOTFOUND);
    /* sid が OS32 でない (FAT 0xA1 は無視) */
    sect[0] = 0x80; sect[1] = 0xA1;
    CHECK(pc98pt_find_os32(sect, 8, 17, 409496, 0, &st, &ln) == PC98PT_ERR_NOTFOUND);
    CHECK(pc98pt_count_used(sect) == 1);
    /* 旧配置 (cdinst が書くバイト列) は標準の読み手には壊れた項目に見える */
    memset(sect, 0, sizeof(sect));
    sect[0] = 0x80; sect[1] = 0xE2;
    sect[6] = 0; sect[7] = 0; sect[8] = 12; sect[9] = 0;         /* 旧: 開始 */
    sect[10] = 16; sect[11] = 7; sect[12] = 0xC2; sect[13] = 0x0B; /* 旧: 終了 */
    CHECK(pc98pt_find_os32(sect, 8, 17, 409496, 0, &st, &ln) != PC98PT_OK);
    /* カーネルの案内用: 旧配置の OS32 項目と分かる (M2) */
    CHECK(pc98pt_os32_is_legacy(sect, 8, 17, 409496) == 1);
    CHECK(pc98pt_os32_is_legacy(sect, 8, 17, 3000) == 0);      /* 旧でもディスクの外 */
    /* 開始ヘッド・セクタが幾何の外 */
    memset(sect, 0, sizeof(sect));
    CHECK(pc98pt_make_os32(&e, 1632, 136, 8, 17) == PC98PT_OK);
    e.start_head = 8;
    CHECK(pc98pt_put(sect, 0, &e) == PC98PT_OK);
    CHECK(pc98pt_find_os32(sect, 8, 17, 0, 0, &st, &ln) == PC98PT_ERR_RANGE);
    e.start_head = 0; e.start_sector = 17;
    CHECK(pc98pt_put(sect, 0, &e) == PC98PT_OK);
    CHECK(pc98pt_find_os32(sect, 8, 17, 0, 0, &st, &ln) == PC98PT_ERR_RANGE);
    /* 終わり <= 開始 */
    e.start_sector = 0; e.end_cyl = 11;
    CHECK(pc98pt_put(sect, 0, &e) == PC98PT_OK);
    CHECK(pc98pt_find_os32(sect, 8, 17, 0, 0, &st, &ln) == PC98PT_ERR_RANGE);
    /* 添字の範囲外 */
    CHECK(pc98pt_get(sect, 16, &e) == PC98PT_ERR_ARG);
    CHECK(pc98pt_put(sect, -1, &e) == PC98PT_ERR_ARG);
    /* 最大値でも 32 ビットに収まる (65535 × 255 × 255 + ...) */
    CHECK(pc98pt_chs_to_lba(65535, 254, 254, 255, 255, &st) == PC98PT_OK);
    CHECK(st == 4261478399UL);
}

/* ======================================================================== */
/*  ATA の指定                                                              */
/* ======================================================================== */

static IdeGeom geom_lba(unsigned long total)
{
    IdeGeom g;
    memset(&g, 0, sizeof(g));
    g.valid = 1;
    g.def_cyl = 16383; g.def_heads = 16; g.def_spt = 63;
    g.w49 = 0x0200;
    g.w53 = 0x0001;
    g.cur_cyl = 16383; g.cur_heads = 16; g.cur_spt = 63;
    g.total = total;
    return g;
}

static void case_ata_lba28(void)
{
    IdeGeom g = geom_lba(16514063UL);
    IdeAddr a;
    u32 limit = 0;

    CHECK(ide_addr_mode(&g, 0, 0, &limit) == IDE_AMODE_LBA28);
    CHECK(limit == 16514063UL);
    CHECK(ide_addr_make(&g, 0, 0x00ABCDEFUL, &a) == IDE_OK);
    CHECK(a.sect_num == 0xEF && a.cyl_lo == 0xCD && a.cyl_hi == 0xAB);
    CHECK(a.drv_head == 0xE0);
    CHECK(ide_addr_make(&g, 1, 0x00FBCDEFUL, &a) == IDE_OK);
    CHECK(a.drv_head == (0xE0 | 0x10 | 0x00));
    /* 総数の最後は通り、その次は断る */
    CHECK(ide_addr_make(&g, 0, 16514062UL, &a) == IDE_OK);
    CHECK(ide_addr_make(&g, 0, 16514063UL, &a) == IDE_ERR_RANGE);
    /* 総数が 2^28 以上の申告でも LBA28 の上限で止まる */
    g.total = 0x20000000UL;
    CHECK(ide_addr_make(&g, 0, 0x0FFFFFFFUL, &a) == IDE_OK);
    CHECK(a.drv_head == (0xE0 | 0x0F));
    /* `dd hd0 lba=268435456` が LBA 0 に化けない */
    CHECK(ide_addr_make(&g, 0, 268435456UL, &a) == IDE_ERR_RANGE);
    CHECK(ide_addr_make(&g, 0, 0xFFFFFFFFUL, &a) == IDE_ERR_RANGE);
    /* 総数 0 の申告は 2^28 を上限 */
    g.total = 0;
    CHECK(ide_addr_mode(&g, 0, 0, &limit) == IDE_AMODE_LBA28 && limit == 0x10000000UL);
}

static void case_ata_range(void)
{
    IdeGeom g = geom_lba(1000UL);

    CHECK(ide_addr_range_ok(&g, 0, 1000) == 1);
    CHECK(ide_addr_range_ok(&g, 999, 1) == 1);
    CHECK(ide_addr_range_ok(&g, 999, 2) == 0);
    CHECK(ide_addr_range_ok(&g, 1000, 0) == 1);           /* 0 本は何もしない */
    CHECK(ide_addr_range_ok(&g, 1000, 1) == 0);
    /* 足すと 32 ビットで 0 付近へ戻る要求 */
    CHECK(ide_addr_range_ok(&g, 0xFFFFFFFFUL, 2) == 0);
    CHECK(ide_addr_range_ok(&g, 10, 0xFFFFFFFFUL) == 0);
    g.valid = 0;
    CHECK(ide_addr_range_ok(&g, 0, 1) == 0);
    CHECK(ide_addr_range_ok((const IdeGeom *)0, 0, 1) == 0);
}

static void case_ata_chs(void)
{
    IdeGeom g;
    IdeAddr a;
    u16 h = 0, s = 0;
    u32 limit = 0;

    /* LBA 無し・現在の幾何が有効 (BIOS が 8/17 に変えた例): 既定 16/63 は使わない */
    memset(&g, 0, sizeof(g));
    g.valid = 1;
    g.def_cyl = 16383; g.def_heads = 16; g.def_spt = 63;
    g.w53 = 0x0001; g.cur_cyl = 60000; g.cur_heads = 8; g.cur_spt = 17;
    CHECK(ide_addr_mode(&g, &h, &s, &limit) == IDE_AMODE_CHS_CUR);
    CHECK(h == 8 && s == 17 && limit == 60000UL * 136UL);
    CHECK(ide_addr_make(&g, 0, 1632, &a) == IDE_OK);
    CHECK(a.sect_num == 1 && a.cyl_lo == 12 && a.cyl_hi == 0 && a.drv_head == 0xA0);
    CHECK(ide_addr_make(&g, 1, 1632 + 136 + 17 + 3, &a) == IDE_OK);
    CHECK(a.sect_num == 4 && a.cyl_lo == 13 && a.drv_head == (0xA0 | 0x10 | 1));
    CHECK(ide_addr_make(&g, 0, 60000UL * 136UL, &a) == IDE_ERR_RANGE);

    /* 現在の幾何が無効 (word 53 bit0 = 0) → 既定 */
    g.w53 = 0;
    CHECK(ide_addr_mode(&g, &h, &s, &limit) == IDE_AMODE_CHS_DEF);
    CHECK(h == 16 && s == 63 && limit == 16383UL * 1008UL);
    /* 現在の幾何が有効と言いつつ 0 */
    g.w53 = 1; g.cur_heads = 0;
    CHECK(ide_addr_mode(&g, &h, &s, &limit) == IDE_AMODE_CHS_DEF);
    /* 既定も 0 → 従来の 8/17、シリンダは 16 ビットで頭打ち */
    memset(&g, 0, sizeof(g));
    g.valid = 1;
    CHECK(ide_addr_mode(&g, &h, &s, &limit) == IDE_AMODE_CHS_DEF);
    CHECK(h == 8 && s == 17 && limit == 65536UL * 136UL);
    CHECK(ide_addr_make(&g, 0, 65536UL * 136UL, &a) == IDE_ERR_RANGE);
    CHECK(ide_addr_make(&g, 0, 65536UL * 136UL - 1UL, &a) == IDE_OK);
    CHECK(a.cyl_lo == 0xFF && a.cyl_hi == 0xFF);
    /* ヘッド欄は 4 ビット: 既定が 17 ヘッドの申告は使わない */
    g.def_heads = 17;
    CHECK(ide_addr_mode(&g, 0, 0, 0) == IDE_AMODE_NONE);
}

/* ======================================================================== */
/*  ext2 の大きさの固定点 (N7)                                              */
/* ======================================================================== */

static void case_layout(void)
{
    Ext2Layout l;

    /* Codex の例: 16,652 セクタ = 8,326 ブロック。素朴には 2 グループで最終
     * グループ 133 ブロック < 必要 135 (SB 1 + GDT 1 + bitmap 2 + inode 表 131)。
     * 固定点で 1 グループ (8,193 ブロック) に落ちる。 */
    CHECK(ext2_layout_plan(16652, 32, &l) == EXT2L_OK);
    CHECK(l.total_blocks == 8193 && l.num_groups == 1);
    CHECK(l.inodes_per_group == 2048 && l.itable_blocks == 256);
    CHECK(l.last_group_need == 1 + 1 + 2 + 256 + 1);   /* + ルートのデータ */
    CHECK(l.last_group_blocks == 8192);

    /* 8MB (既存の試験の大きさ) は 1 グループのまま */
    CHECK(ext2_layout_plan(16384, 32, &l) == EXT2L_OK);
    CHECK(l.total_blocks == 8192 && l.num_groups == 1 && l.inodes_per_group == 2048);

    /* 36,000 セクタ (b8 の 3 グループ) は切り下げない: 最終 (g2 非スパース) 1615 */
    CHECK(ext2_layout_plan(36000, 32, &l) == EXT2L_OK);
    CHECK(l.total_blocks == 18000 && l.num_groups == 3);
    CHECK(l.last_group_blocks == 1615);
    CHECK(l.last_group_need == 2 + l.itable_blocks);

    /* 32 / 33 グループの境界。256MiB = 524,288 セクタ = 262,144 ブロック */
    CHECK(ext2_layout_plan(524288, 32, &l) == EXT2L_OK);
    CHECK(l.num_groups == 32 && l.total_blocks == 262144);
    /* 262,145 ブロック = 32 グループ (最終 8192)、262,146 からが 33 グループ。
     * 上限を超える長さは切り下げずに断る (段 1 は 256MiB 以下) */
    CHECK(ext2_layout_plan(262144UL * 2UL + 1UL, 32, &l) == EXT2L_OK);  /* 端数セクタは捨てる */
    CHECK(ext2_layout_plan(262145UL * 2UL, 32, &l) == EXT2L_OK);
    CHECK(l.num_groups == 32 && l.last_group_blocks == 8192);
    CHECK(ext2_layout_plan(262146UL * 2UL, 32, &l) == EXT2L_ERR_GROUPS);
    /* 上限を 33 にすると 33 グループ目 (1 ブロック) は落とされて 32 に戻る */
    CHECK(ext2_layout_plan(262146UL * 2UL, 33, &l) == EXT2L_OK);
    CHECK(l.num_groups == 32 && l.total_blocks == 262145UL);

    /* 最終グループの切り下げ: g=2 (非スパース) と g=3 (スパース) の境目を
     * 1 ブロックずつ掃く。落とした大きさでは**素朴な配置が本当に足りない**
     * (layout_fill で見る)、落とさなかった大きさでは足りている、の両方が出ること */
    {
        u32 g, tb;
        for (g = 2; g <= 3; g++) {
            int dropped = 0, kept = 0, bad = 0;
            u32 base = 1UL + g * 8192UL;
            for (tb = base + 1UL; tb < base + 400UL; tb++) {
                Ext2Layout raw, a;
                CHECK(layout_fill(tb, 32, &raw) == EXT2L_OK);
                CHECK(ext2_layout_plan(tb * 2UL, 32, &a) == EXT2L_OK);
                if (raw.last_group_blocks < raw.last_group_need) {
                    dropped++;
                    if (a.num_groups != g || a.total_blocks != base) bad++;
                } else {
                    kept++;
                    if (a.num_groups != g + 1 || a.total_blocks != tb) bad++;
                }
                if (a.last_group_blocks < a.last_group_need) bad++;
            }
            CHECK(bad == 0);
            CHECK(dropped > 0 && kept > 0);
        }
        CHECK(!ext2_layout_is_sparse(2) && ext2_layout_is_sparse(3));
        CHECK(ext2_layout_is_sparse(9) && ext2_layout_is_sparse(25) &&
              ext2_layout_is_sparse(49) && !ext2_layout_is_sparse(6));
    }
    /* 小さすぎる */
    CHECK(ext2_layout_plan(127, 32, &l) == EXT2L_ERR_SMALL);
    CHECK(ext2_layout_plan(128, 32, &l) == EXT2L_OK);
    /* 全長で: どの大きさでも最終グループに必要量が収まり、長さを超えない */
    {
        u32 sec;
        int bad = 0;
        for (sec = 128; sec <= 524288UL; sec += 997UL) {
            Ext2Layout a;
            if (ext2_layout_plan(sec, 32, &a) != EXT2L_OK) { bad++; continue; }
            if (a.total_blocks * 2UL > sec) bad++;
            if (a.last_group_blocks < a.last_group_need) bad++;
        }
        CHECK(bad == 0);
    }
}

/* ======================================================================== */
/*  hdprep                                                                  */
/* ======================================================================== */

static HdprepGeom hp_geom(unsigned long h, unsigned long s, unsigned long c,
                          unsigned long total, int mode)
{
    HdprepGeom g;
    memset(&g, 0, sizeof(g));
    g.ata_present = 1;
    g.addr_mode = mode;
    g.ata_total = total;
    g.bios_queried = 1;
    g.bios_valid = 1;
    g.bios_cyl = c;
    g.bios_heads = h;
    g.bios_spt = s;
    g.bios_seclen = 512;
    g.ata_cur_cyl = 16383;
    g.ata_cur_heads = 16;
    g.ata_cur_spt = 63;
    return g;
}

static void case_hdprep_geom(void)
{
    HdprepGeom g = hp_geom(16, 63, 16382, 16514063UL, HDPREP_AMODE_LBA28);

    CHECK(hdprep_check_geom(&g) == HDPREP_OK);
    g.addr_mode = HDPREP_AMODE_CHS_CUR;
    CHECK(hdprep_check_geom(&g) == HDPREP_OK);
    g.addr_mode = HDPREP_AMODE_CHS_DEF;             /* LBA も現在の幾何も無い */
    CHECK(hdprep_check_geom(&g) == HDPREP_E_ADDR);
    g.addr_mode = HDPREP_AMODE_NONE;
    CHECK(hdprep_check_geom(&g) == HDPREP_E_ADDR);
    g = hp_geom(16, 63, 16382, 16514063UL, HDPREP_AMODE_LBA28);
    g.ata_present = 0;
    CHECK(hdprep_check_geom(&g) == HDPREP_E_NO_ATA);
    g = hp_geom(16, 63, 16382, 0UL, HDPREP_AMODE_LBA28);   /* 総数の申告なし (C3) */
    CHECK(hdprep_check_geom(&g) == HDPREP_E_NO_TOTAL);
    g.addr_mode = HDPREP_AMODE_CHS_CUR;
    CHECK(hdprep_check_geom(&g) == HDPREP_E_NO_TOTAL);
    g = hp_geom(16, 63, 16382, 16514063UL, HDPREP_AMODE_LBA28);
    g.bios_queried = 0;                              /* HDD ローダが問い合わせなかった */
    CHECK(hdprep_check_geom(&g) == HDPREP_E_NO_BIOS);
    g.bios_queried = 1; g.bios_valid = 0;            /* 情報域が無効 / 規則外 */
    CHECK(hdprep_check_geom(&g) == HDPREP_E_NO_BIOS);
    g.bios_valid = 1; g.bios_seclen = 256;           /* BX ≠ 512 */
    CHECK(hdprep_check_geom(&g) == HDPREP_E_SECLEN);
    g.bios_seclen = 512; g.bios_heads = 0;
    CHECK(hdprep_check_geom(&g) == HDPREP_E_NO_BIOS);
    CHECK(hdprep_check_geom((const HdprepGeom *)0) == HDPREP_E_ARG);
}

static void case_hdprep_disk(void)
{
    unsigned char l0[512], l1[512];
    PC98PartEntry e;

    memset(l0, 0, sizeof(l0));
    memset(l1, 0, sizeof(l1));
    CHECK(hdprep_check_disk(l0, l1) == HDPREP_OK);
    /* LBA 0 に 55AA */
    l0[510] = 0x55; l0[511] = 0xAA;
    CHECK(hdprep_check_disk(l0, l1) == HDPREP_E_MBR_SIG);
    l0[510] = 0; l0[511] = 0;
    /* LBA 1 のどこかの項目 (最後の 1 つでも) */
    l1[15 * 32 + 1] = 0x81;
    CHECK(hdprep_check_disk(l0, l1) == HDPREP_E_PT_USED);
    memset(l1, 0, sizeof(l1));
    l1[7 * 32 + 0] = 0x80;                          /* mid だけ */
    CHECK(hdprep_check_disk(l0, l1) == HDPREP_E_PT_USED);
    /* OS32 自身の区画も「空でない」 (hdprep は作り直さない、N4) */
    memset(l1, 0, sizeof(l1));
    CHECK(pc98pt_make_os32(&e, 1632, 136 * 100, 8, 17) == PC98PT_OK);
    CHECK(pc98pt_put(l1, 0, &e) == PC98PT_OK);
    CHECK(hdprep_check_disk(l0, l1) == HDPREP_E_PT_USED);
    CHECK(hdprep_check_disk((const unsigned char *)0, l1) == HDPREP_E_ARG);
}

static void case_hdprep_mounts(void)
{
    CHECK(hdprep_check_mounts(0, 0) == HDPREP_OK);
    CHECK(hdprep_check_mounts(1, 0) == HDPREP_NEED_UMOUNT);
    CHECK(hdprep_check_mounts(2, 0) == HDPREP_NEED_UMOUNT);  /* 別 prefix にも */
    CHECK(hdprep_check_mounts(1, 1) == HDPREP_E_ROOT);
    CHECK(hdprep_check_mounts(0, 1) == HDPREP_E_ROOT);
    CHECK(hdprep_check_mounts(-9, 0) == HDPREP_E_ARG);
}

static void case_hdprep_plan(void)
{
    HdprepGeom g;
    HdprepPlan p;
    unsigned long mb;

    CHECK(hdprep_parse_mb((const char *)0, &mb) == HDPREP_OK && mb == 256);
    CHECK(hdprep_parse_mb("", &mb) == HDPREP_OK && mb == 256);
    CHECK(hdprep_parse_mb("64", &mb) == HDPREP_OK && mb == 64);
    CHECK(hdprep_parse_mb("8", &mb) == HDPREP_OK && mb == 8);
    CHECK(hdprep_parse_mb("7", &mb) == HDPREP_E_SIZE);
    CHECK(hdprep_parse_mb("257", &mb) == HDPREP_E_SIZE);
    CHECK(hdprep_parse_mb("99999999999999999999", &mb) == HDPREP_E_SIZE);
    CHECK(hdprep_parse_mb("12x", &mb) == HDPREP_E_SIZE);
    CHECK(hdprep_parse_mb("-5", &mb) == HDPREP_E_SIZE);

    /* 実機の見込み 16/63: 開始 2016、256MiB をシリンダへ切り下げ */
    g = hp_geom(16, 63, 16382, 16514063UL, HDPREP_AMODE_LBA28);
    CHECK(hdprep_plan(&g, 256, &p) == HDPREP_OK);
    CHECK(p.start == 2016 && p.cyl_sectors == 1008);
    CHECK(p.len == 520 * 1008);                     /* 524288 / 1008 = 520.1 */
    CHECK(p.end_cyl == 2 + 520 - 1);
    CHECK(p.probe_lba == 2016 + 520 * 1008 - 1);
    CHECK(p.len <= 256UL * 2048UL);

    /* 8/17 (NP21/W の NHD): 開始 1632 */
    g = hp_geom(8, 17, 3011, 409496UL, HDPREP_AMODE_LBA28);
    CHECK(hdprep_plan(&g, 64, &p) == HDPREP_OK);
    CHECK(p.start == 1632 && p.len == 963 * 136 && p.end_cyl == 12 + 963 - 1);
    /* 200MB の NHD に 256MiB は入らない → 入るだけ (シリンダ単位) */
    CHECK(hdprep_plan(&g, 256, &p) == HDPREP_OK);
    CHECK(p.start + p.len <= 409496UL && p.len == (3011 - 12) * 136);

    /* BIOS の CX が IDENTIFY の総数より小さい → 小さい方 */
    g = hp_geom(8, 17, 100, 409496UL, HDPREP_AMODE_LBA28);
    CHECK(hdprep_plan(&g, 8, &p) == HDPREP_E_TOO_SMALL);   /* 88 シリンダ < 8MiB */
    g = hp_geom(8, 17, 200, 409496UL, HDPREP_AMODE_LBA28);
    CHECK(hdprep_plan(&g, 256, &p) == HDPREP_OK);
    CHECK(p.start + p.len <= 200UL * 136UL);
    /* IDENTIFY の総数が BIOS 幾何の容量より小さい → 総数で頭打ち */
    g = hp_geom(16, 63, 16382, 2016UL + 1008UL * 100UL + 500UL, HDPREP_AMODE_LBA28);
    CHECK(hdprep_plan(&g, 256, &p) == HDPREP_OK);
    CHECK(p.len == 1008UL * 100UL && p.disk_limit == 2016UL + 1008UL * 100UL + 500UL);
    /* 現在の CHS の容量が総数より小さい (CHS_CUR) → CHS の容量で頭打ち (C4) */
    g = hp_geom(16, 63, 16382, 16514063UL, HDPREP_AMODE_CHS_CUR);
    g.ata_cur_cyl = 102; g.ata_cur_heads = 16; g.ata_cur_spt = 63;
    CHECK(hdprep_plan(&g, 256, &p) == HDPREP_OK);
    CHECK(p.start + p.len <= 102UL * 1008UL && p.len == 100UL * 1008UL);
    /* LBA28 は 2^28 で頭打ち (BIOS と総数がそれより大きい申告) */
    g = hp_geom(255, 63, 65535, 0xFFFFFFF0UL, HDPREP_AMODE_LBA28);
    CHECK(hdprep_plan(&g, 256, &p) == HDPREP_OK);
    CHECK(p.disk_limit == 0x10000000UL);
    /* ディスクが開始より小さい */
    g = hp_geom(8, 17, 3011, 1000UL, HDPREP_AMODE_LBA28);
    CHECK(hdprep_plan(&g, 8, &p) == HDPREP_E_TOO_SMALL);
    /* 幾何が通らなければ計画しない */
    g = hp_geom(8, 17, 3011, 409496UL, HDPREP_AMODE_CHS_DEF);
    CHECK(hdprep_plan(&g, 8, &p) == HDPREP_E_ADDR);
    g = hp_geom(8, 17, 3011, 409496UL, HDPREP_AMODE_LBA28);
    CHECK(hdprep_plan(&g, 300, &p) == HDPREP_E_SIZE);
    /* 計画した区画は共有部の書き手がそのまま受け取れる */
    g = hp_geom(16, 63, 16382, 16514063UL, HDPREP_AMODE_LBA28);
    CHECK(hdprep_plan(&g, 256, &p) == HDPREP_OK);
    {
        PC98PartEntry e;
        unsigned char s[512];
        unsigned long st, ln;
        memset(s, 0, sizeof(s));
        CHECK(pc98pt_make_os32(&e, p.start, p.len, p.heads, p.spt) == PC98PT_OK);
        CHECK(pc98pt_put(s, 0, &e) == PC98PT_OK);
        CHECK(pc98pt_find_os32(s, 16, 63, 16514063UL, 0, &st, &ln) == PC98PT_OK);
        CHECK(st == p.start && ln == p.len);
        /* この長さに ext2 が作れる (固定点が成立する) */
        {
            Ext2Layout l;
            CHECK(ext2_layout_plan(p.len, 32, &l) == EXT2L_OK);
            CHECK(l.total_blocks * 2UL <= p.len);
        }
    }
}

/* ======================================================================== */
/*  Python (tools/pc98pt.py) との突き合わせ用: 項目のバイト列を 16 進で出す */
/* ======================================================================== */
static int dump_entry(int argc, char **argv)
{
    PC98PartEntry e;
    unsigned char s[512];
    int i, rc;

    if (argc != 6) return 2;
    memset(s, 0, sizeof(s));
    rc = pc98pt_make_os32(&e, strtoul(argv[2], 0, 10), strtoul(argv[3], 0, 10),
                          strtoul(argv[4], 0, 10), strtoul(argv[5], 0, 10));
    if (rc != PC98PT_OK) { printf("ERR %d\n", rc); return 0; }
    pc98pt_put(s, 0, &e);
    for (i = 0; i < 32; i++) printf("%02x", s[i]);
    printf("\n");
    return 0;
}

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "";

    if (!strcmp(c, "dump_entry")) return dump_entry(argc, argv);
    if (!strcmp(c, "pt_offsets")) case_pt_offsets();
    else if (!strcmp(c, "pt_817")) case_pt_817();
    else if (!strcmp(c, "pt_1663")) case_pt_1663();
    else if (!strcmp(c, "pt_reject")) case_pt_reject();
    else if (!strcmp(c, "ata_lba28")) case_ata_lba28();
    else if (!strcmp(c, "ata_range")) case_ata_range();
    else if (!strcmp(c, "ata_chs")) case_ata_chs();
    else if (!strcmp(c, "layout")) case_layout();
    else if (!strcmp(c, "hdprep_geom")) case_hdprep_geom();
    else if (!strcmp(c, "hdprep_disk")) case_hdprep_disk();
    else if (!strcmp(c, "hdprep_mounts")) case_hdprep_mounts();
    else if (!strcmp(c, "hdprep_plan")) case_hdprep_plan();
    else { fprintf(stderr, "unknown case %s\n", c); return 2; }
    return failed ? 1 : 0;
}
