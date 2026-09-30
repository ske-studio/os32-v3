/* ======================================================================== */
/*  CMD_HDPREP.C — `hdprep [MB]`: 空の hd0 に OS32 の一時置き場を作る         */
/*                                                                          */
/*  票 docs/archive/realhw_v21/TASK_HDD_INSTALL.md 段 1-7 / §1-v3 N4・N6・R3-1。   */
/*  hd0 (= DA 80h) 専用・空のディスク専用。手順 (R3-1 ユーザー決裁 B):       */
/*                                                                          */
/*   1. 検査 (1 つでも欠けたら 1 バイトも書かない)                           */
/*      BIOS 幾何 (INT 1Bh AH=84h) がある・BX = 512・I/O が LBA28 か現在の   */
/*      CHS・LBA 1 に区画項目が無い・LBA 0 に 55AA が無い・hd0 がルートでない */
/*   2. 表示: ドライブ・BIOS / ATA 幾何・書く LBA 範囲・LBA 0 / 1 の生の中身  */
/*   3. `yes` の入力                                                         */
/*   4. hd0 がマウント中なら sys_umount_checked (sync の失敗も拾う)、        */
/*      それでも残っていれば断る                                              */
/*   5. 予定域の最終セクタへ 1 セクタの write → readback (探り)             */
/*   6. ext2_format_at (区画表を読まずに範囲だけに作る)                      */
/*   7. 区画表 (LBA 1) を書いて読み戻し比較                                  */
/*   8. 通常のマウント (/hd0) → 書き → 読み戻し → 消す                       */
/*   6〜8 の失敗は「INCOMPLETE」と出して止める (空のディスクなので失う        */
/*   データは無い。区画表を書く前の失敗ならディスクは空のまま扱える)。       */
/*                                                                          */
/*  判定と計画は hdprep_plan.c (純粋関数、ホスト試験の対象)。区画表の中身は */
/*  drivers/pc98pt.c (カーネル・ローダ・nhd_deploy.py と共有) で作る。      */
/* ======================================================================== */

#include "shell.h"
#include "hdprep_plan.h"
#include "drivers/pc98pt.h"   /* PROGRAM_FLAGS の -I. で引く */

#define HDPREP_SECT          512
#define HDPREP_DUMP_BYTES    64      /* LBA 0 / 1 の先頭を何バイト見せるか */
#define HDPREP_ANSWER_MAX    8
#define HDPREP_CHECK_FILE    "/hd0/.hdprep"
#define HDPREP_CHECK_BYTES   1024
#define HDPREP_MOUNT_POINT   "/hd0"
#define HDPREP_DEV           "hd0"

static u8 hp_lba0[HDPREP_SECT];
static u8 hp_lba1[HDPREP_SECT];
static u8 hp_sect[HDPREP_SECT];
static u8 hp_back[HDPREP_SECT];
static u8 hp_file[HDPREP_CHECK_BYTES];
static u8 hp_fback[HDPREP_CHECK_BYTES];

static int hp_memeq(const u8 *a, const u8 *b, int n)
{
    int i;
    for (i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}

static int hp_streq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static void hp_refuse(int code)
{
    g_api->kprintf(ATTR_RED, "hdprep: refused: %s (code %d). Nothing was written.\n",
                   hdprep_reason(code), code);
}

static void hp_incomplete(const char *what, int rc)
{
    g_api->kprintf(ATTR_RED, "hdprep: INCOMPLETE: %s (rc=%d)\n", what, rc);
    g_api->kprintf(ATTR_RED, "%s",
                   "  The area is not usable. Run hdprep again only after checking "
                   "LBA 0/1 (dd hd0 lba=1).\n");
}

static void hp_dump(const char *label, const u8 *b)
{
    int i, nz = 0;

    for (i = 0; i < HDPREP_SECT; i++) if (b[i]) nz++;
    g_api->kprintf(ATTR_WHITE, "  %s: nonzero=%d sig=%02x%02x\n",
                   label, nz, (u32)b[510], (u32)b[511]);
    for (i = 0; i < HDPREP_DUMP_BYTES; i++) {
        if ((i % 16) == 0) g_api->kprintf(ATTR_WHITE, "    %03x:", (u32)i);
        g_api->kprintf(ATTR_WHITE, " %02x", (u32)b[i]);
        if ((i % 16) == 15) g_api->kprintf(ATTR_WHITE, "%s", "\n");
    }
}

/* `yes` の入力。Enter までの 1 行を読み、"yes" ちょうどなら 1。 */
static int hp_ask_yes(void)
{
    char ans[HDPREP_ANSWER_MAX];
    int len = 0, key, over = 0;

    g_api->kprintf(ATTR_YELLOW, "%s", "Type 'yes' to write hd0 (anything else cancels): ");
    for (;;) {
        key = g_api->kbd_getchar() & 0xFF;
        if (key == 0x0D || key == '\n') break;
        if (key == 0x08) {
            if (len > 0) {
                len--;
#ifdef SHELL_AS_APP
                sh_erase_cells(ans[len]);
#else
                g_api->shell_putchar(0x08, ATTR_WHITE);
#endif
            }
            continue;
        }
        if (key < 0x20 || key >= 0x7F) continue;
        if (len >= HDPREP_ANSWER_MAX - 1) { over = 1; continue; }
        ans[len++] = (char)key;
        g_api->shell_putchar((char)key, ATTR_WHITE);
    }
    ans[len] = '\0';
    g_api->shell_putchar('\n', ATTR_WHITE);
    return (!over && hp_streq(ans, "yes")) ? 1 : 0;
}

static void hp_geom_from_kapi(const HddGeom *hg, HdprepGeom *g)
{
    g->ata_present  = hg->ata_present;
    g->addr_mode    = hg->addr_mode;
    g->ata_total    = hg->ata_total;
    g->ata_cur_cyl  = hg->ata_cur_cyl;
    g->ata_cur_heads = hg->ata_cur_heads;
    g->ata_cur_spt  = hg->ata_cur_spt;
    g->bios_queried = hg->bios_queried;
    g->bios_valid   = hg->bios_valid;
    g->bios_cyl     = hg->bios_cyl;
    g->bios_heads   = hg->bios_heads;
    g->bios_spt     = hg->bios_spt;
    g->bios_seclen  = hg->bios_seclen;
}

static const char *hp_amode_name(int m)
{
    switch (m) {
    case HDPREP_AMODE_LBA28:   return "lba28";
    case HDPREP_AMODE_CHS_CUR: return "chs-current";
    case HDPREP_AMODE_CHS_DEF: return "chs-default";
    default:                   return "none";
    }
}

/* 通常のマウントで書いて読み戻して消す (R3-1 の確認) */
static int hp_verify_mount(void)
{
    int fd, n, i, rc;

    rc = g_api->sys_mount(HDPREP_MOUNT_POINT, HDPREP_DEV, "ext2");
    if (rc != 0) { hp_incomplete("mount /hd0 failed", rc); return -1; }

    for (i = 0; i < HDPREP_CHECK_BYTES; i++) hp_file[i] = (u8)(i * 7 + 1);
    fd = g_api->sys_open(HDPREP_CHECK_FILE, KAPI_O_WRONLY | KAPI_O_CREAT | KAPI_O_TRUNC);
    if (fd < 0) { hp_incomplete("create check file failed", fd); return -1; }
    n = g_api->sys_write(fd, hp_file, HDPREP_CHECK_BYTES);
    g_api->sys_close(fd);   /* void — ext2 は write-through なので書いた時点で媒体へ */
    if (n != HDPREP_CHECK_BYTES) {
        hp_incomplete("write check file failed", n);
        return -1;
    }
    fd = g_api->sys_open(HDPREP_CHECK_FILE, KAPI_O_RDONLY);
    if (fd < 0) { hp_incomplete("reopen check file failed", fd); return -1; }
    n = g_api->sys_read(fd, hp_fback, HDPREP_CHECK_BYTES);
    g_api->sys_close(fd);
    if (n != HDPREP_CHECK_BYTES || !hp_memeq(hp_file, hp_fback, HDPREP_CHECK_BYTES)) {
        hp_incomplete("check file read back differs", n);
        return -1;
    }
    rc = g_api->sys_unlink(HDPREP_CHECK_FILE);
    if (rc < 0) { hp_incomplete("remove check file failed", rc); return -1; }
    return 0;
}

static int cmd_hdprep(int argc, char **argv)
{
    HddGeom hg;
    HdprepGeom g;
    HdprepPlan p;
    PC98PartEntry pe;
    unsigned long mb;
    int rc, i, mounts, root_hd0;
    const char *rootdev;

    if (argc > 2) {
        shell_print_help(argv[0]);
        return SH_STATUS_USAGE;
    }
    rc = hdprep_parse_mb(argc > 1 ? argv[1] : (const char *)0, &mb);
    if (rc != HDPREP_OK) { hp_refuse(rc); return SH_STATUS_USAGE; }

    /* ---- 1. 幾何 ---- */
    if (g_api->hdd_geom_info(HDPREP_DRIVE, &hg) != 0) {
        hp_refuse(HDPREP_E_ARG);
        return SH_STATUS_ERROR;
    }
    hp_geom_from_kapi(&hg, &g);

    g_api->kprintf(ATTR_CYAN, "%s", "hdprep: target hd0 (IDE drive 0 = BIOS DA 80h)\n");
    g_api->kprintf(ATTR_WHITE,
                   "  BIOS (AH=84h): queried=%d valid=%d C/H/S=%u/%u/%u len=%u\n",
                   (int)hg.bios_queried, (int)hg.bios_valid, (u32)hg.bios_cyl,
                   (u32)hg.bios_heads, (u32)hg.bios_spt, (u32)hg.bios_seclen);
    g_api->kprintf(ATTR_WHITE,
                   "  ATA: present=%d def=%u/%u/%u cur=%u/%u/%u(%s) lba=%d total=%u io=%s\n",
                   (int)hg.ata_present, (u32)hg.ata_def_cyl, (u32)hg.ata_def_heads,
                   (u32)hg.ata_def_spt, (u32)hg.ata_cur_cyl, (u32)hg.ata_cur_heads,
                   (u32)hg.ata_cur_spt, (hg.ata_w53 & 1) ? "valid" : "invalid",
                   (hg.ata_w49 & 0x0200) ? 1 : 0, hg.ata_total,
                   hp_amode_name(hg.addr_mode));

    rc = hdprep_check_geom(&g);
    if (rc != HDPREP_OK) { hp_refuse(rc); return SH_STATUS_ERROR; }

    /* ---- 1. ディスクが空か (LBA 0 / 1 の生の中身も見せる) ---- */
    rc = g_api->ide_read_sector(HDPREP_DRIVE, 0, hp_lba0);
    if (rc != 0) {
        g_api->kprintf(ATTR_RED, "hdprep: refused: cannot read LBA 0 (rc=%d). Nothing was written.\n", rc);
        return SH_STATUS_ERROR;
    }
    rc = g_api->ide_read_sector(HDPREP_DRIVE, PC98PT_LBA, hp_lba1);
    if (rc != 0) {
        g_api->kprintf(ATTR_RED, "hdprep: refused: cannot read LBA 1 (rc=%d). Nothing was written.\n", rc);
        return SH_STATUS_ERROR;
    }
    hp_dump("LBA 0", hp_lba0);
    hp_dump("LBA 1", hp_lba1);
    rc = hdprep_check_disk(hp_lba0, hp_lba1);
    if (rc != HDPREP_OK) { hp_refuse(rc); return SH_STATUS_ERROR; }

    /* ---- 1. マウント ---- */
    rootdev = g_api->vfs_devname("/");
    root_hd0 = (rootdev && hp_streq(rootdev, HDPREP_DEV)) ? 1 : 0;
    mounts = g_api->dev_mount_count(HDPREP_DRIVE);
    rc = hdprep_check_mounts(mounts, root_hd0);
    if (rc < 0) { hp_refuse(rc); return SH_STATUS_ERROR; }

    /* ---- 計画と表示 ---- */
    {
        int prc = hdprep_plan(&g, mb, &p);
        if (prc != HDPREP_OK) { hp_refuse(prc); return SH_STATUS_ERROR; }
    }
    g_api->kprintf(ATTR_WHITE,
                   "  area: LBA %u..%u (%u sectors = %u KiB), cyl %u..%u of %u/%u\n",
                   p.start, p.start + p.len - 1UL, p.len, p.len / 2UL,
                   p.start / p.cyl_sectors, p.end_cyl, p.heads, p.spt);
    g_api->kprintf(ATTR_WHITE,
                   "  writes: probe LBA %u, ext2 in the area, partition table LBA 1\n",
                   p.probe_lba);
    if (rc == HDPREP_NEED_UMOUNT)
        g_api->kprintf(ATTR_YELLOW, "  hd0 is mounted (%d) and will be unmounted first\n",
                       mounts);

    if (!hp_ask_yes()) {
        g_api->kprintf(ATTR_YELLOW, "%s", "hdprep: cancelled. Nothing was written.\n");
        return SH_STATUS_USAGE;
    }

    /* ---- 4. アンマウント (書く前の最後の検査) ---- */
    if (rc == HDPREP_NEED_UMOUNT) {
        if (g_api->sys_is_mounted(HDPREP_MOUNT_POINT)) {
            int urc = g_api->sys_umount_checked(HDPREP_MOUNT_POINT);
            if (urc < 0) {
                g_api->kprintf(ATTR_RED,
                               "hdprep: refused: umount /hd0 failed (rc=%d). Nothing was written.\n",
                               urc);
                return SH_STATUS_ERROR;
            }
        }
        if (g_api->dev_mount_count(HDPREP_DRIVE) != 0) {
            hp_refuse(HDPREP_E_STILL_MOUNTED);
            return SH_STATUS_ERROR;
        }
    }

    /* ---- 5. 探り: 予定域の最終セクタへ書いて読み戻す ---- */
    for (i = 0; i < HDPREP_SECT; i++) hp_sect[i] = (u8)(0xA5 ^ i);
    rc = g_api->ide_write_sector(HDPREP_DRIVE, p.probe_lba, hp_sect);
    if (rc == 0) rc = g_api->ide_read_sector(HDPREP_DRIVE, p.probe_lba, hp_back);
    if (rc != 0 || !hp_memeq(hp_sect, hp_back, HDPREP_SECT)) {
        g_api->kprintf(ATTR_RED,
                       "hdprep: probe write/readback at LBA %u failed (rc=%d). "
                       "Only that sector was written; partition table untouched.\n",
                       p.probe_lba, rc);
        return SH_STATUS_ERROR;
    }

    /* ---- 6. ext2 (区画表を読まずに範囲だけ) ---- */
    g_api->kprintf(ATTR_CYAN, "%s", "hdprep: formatting ext2...\n");
    rc = g_api->ext2_format_at(HDPREP_DRIVE, p.start, p.len);
    if (rc != 0) {
        hp_incomplete("ext2_format_at failed (partition table not written)", rc);
        return SH_STATUS_ERROR;
    }

    /* ---- 7. 区画表 → 読み戻し比較 ---- */
    for (i = 0; i < HDPREP_SECT; i++) hp_sect[i] = 0;
    rc = pc98pt_make_os32(&pe, p.start, p.len, p.heads, p.spt);
    if (rc == PC98PT_OK) rc = pc98pt_put(hp_sect, 0, &pe);
    if (rc != PC98PT_OK) {
        hp_incomplete("cannot build partition entry (partition table not written)", rc);
        return SH_STATUS_ERROR;
    }
    rc = g_api->ide_write_sector(HDPREP_DRIVE, PC98PT_LBA, hp_sect);
    if (rc == 0) rc = g_api->ide_read_sector(HDPREP_DRIVE, PC98PT_LBA, hp_back);
    if (rc != 0 || !hp_memeq(hp_sect, hp_back, HDPREP_SECT)) {
        hp_incomplete("partition table write/readback failed", rc);
        return SH_STATUS_ERROR;
    }

    /* ---- 8. 通常のマウントで確認 ---- */
    if (hp_verify_mount() != 0) return SH_STATUS_ERROR;

    g_api->kprintf(ATTR_GREEN,
                   "hdprep: done. /hd0 = ext2 at LBA %u (%u sectors). "
                   "It is mounted at boot from now on.\n", p.start, p.len);
    return SH_STATUS_OK;
}

static const ShellCmd hdprep_cmds[] = {
    { "hdprep", cmd_hdprep, "[MB]",
      "Create a temporary OS32 ext2 area on an EMPTY hd0 (8..256 MiB, default 256)" },
    { (const char *)0, 0, 0, 0 }
};

void shell_cmd_hdprep_init(void)
{
    shell_register_cmds(hdprep_cmds);
}
