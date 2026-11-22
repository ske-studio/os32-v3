#include "userland/system/inst_hdd.h"
#include "fs/pc98pt.h"
#include "install_geometry.h"

#include "include/ide_contract.h"

int platform_install_identify(KernelAPI *api)
{
    static IdeInfo info;

    /* IDEの初期化とディスク情報の取得 (表示用。判定は hdd_geom_info) */
    api->kprintf(KLOG_ATTR, "%s", "Initializing IDE controller...\n");
    api->ide_init();

    if (!api->ide_drive_present(INST_DRIVE)) {
        api->kprintf(0x4F, "%s", "Error: IDE Master Drive (hd0) not found.\n");
        return -1;
    }

    if (api->ide_identify(INST_DRIVE, &info) != 0) {
        api->kprintf(0x4F, "%s", "Error: Could not identify drive.\n");
        return -1;
    }

    api->kprintf(ATTR_WHITE, "Target Drive Found: %s\n", info.model);
    api->kprintf(ATTR_WHITE, "Size: %u MB (%u sectors)\n", info.size_mb, info.total_sectors);

    return 0;
}

#define IH_SECT 512

static u8 ih_lba0[IH_SECT];
static u8 ih_lba1[IH_SECT];
static u8 ih_sect[IH_SECT];
static u8 ih_back[IH_SECT];

/* この実行で ERASE を受けて LBA 0/1 を消したか (消す書き込みを始めた時点で 1)、
 * その後で区画表 (LBA 1) を書きに行ったか。inst_hdd_check が毎回 0 に戻す。
 * 失敗の案内 (空のディスクとして入れ直せる / OS32 の区域を作り直せる) を選ぶ */
static int ih_erased;
static int ih_pt_written;

static int ih_memeq(const u8 *a, const u8 *b, int n)
{
    int i;
    for (i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}

static int target_streq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

/* ERASE で消した後に止まったときの案内 (区画表は消した、もう一度実行すれば
 * 空のディスクとして入れられる) */
static void ih_erased_hint(KernelAPI *api)
{
    api->kprintf(ATTR_YELLOW, "%s",
                 "  hd0's partition table was ERASED (LBA 0 and 1 are zero). Run the\n"
                 "  installer again: it installs onto hd0 as an empty disk.\n");
}

/* 書く前に断る (umount も呼んでいないので「何も書いていない」と言える) */
static void target_refuse(KernelAPI *api, int code)
{
    api->kprintf(ATTR_RED, "Refused: %s (code %d). Nothing was written.\n",
                 inst_reason(code), code);
}

/* 他の OS の区画・起動域がある: そのままでは入れない。消す道は y の後の ERASE */
static void ih_foreign_hint(KernelAPI *api)
{
    api->kprintf(ATTR_YELLOW, "%s",
                 "  hd0 holds another system's partitions or boot code. OS32 does not\n"
                 "  install next to them: the only way onto this disk is to ERASE its whole\n"
                 "  partition table (asked after the confirmation below).\n");
}

/* 区画表を直さずに作り直すときの案内 (ゲストの外の手当て。ゲストでは ERASE) */
static void ih_host_hint(KernelAPI *api)
{
    api->kprintf(ATTR_YELLOW, "%s",
                 "  The installer does not repair such a table. Outside OS32 it can be\n"
                 "  cleared or rebuilt: on NP21/W recreate the NHD on the host (make nhd-init\n"
                 "  = tools/nhd_deploy.py init, NP21/W stopped); on real hardware use another\n"
                 "  tool.\n");
}

void inst_hdd_incomplete(KernelAPI *api, const char *what, int rc)
{
    api->kprintf(ATTR_RED, "INCOMPLETE: %s (rc=%d)\n", what, rc);
    /* 同じ起動のまま再実行すると通らないことがある (ext2 の fs_error・古い
     * マウントの状態)。再起動してから入れ直す */
    api->kprintf(ATTR_RED, "%s",
                 "  hd0 is not a usable OS32 disk. REBOOT, then run the installer again\n"
                 "  (it re-creates the OS32 area).\n");
    /* ERASE の後、区画表を書く前に止まった: LBA 0/1 は 0 のまま */
    if (ih_erased && !ih_pt_written) ih_erased_hint(api);
}

/* 1 セクタを書いて読み戻して比べる */
static int ih_write_verify(KernelAPI *api, u32 lba, const u8 *buf)
{
    int rc = api->ide_write_sector(INST_DRIVE, lba, buf);
    if (rc == 0) rc = api->ide_read_sector(INST_DRIVE, lba, ih_back);
    if (rc != 0) return rc;
    return ih_memeq(buf, ih_back, IH_SECT) ? 0 : -1;
}

static void ih_refused_after_umount(KernelAPI *api)
{
    api->kprintf(ATTR_RED, "%s",
                 "  Nothing was erased or formatted. (Unmounting may have flushed\n"
                 "  hd0's file system data, as any umount does.)\n");
}

/* /hd0 の hd0 を外す (N6)。外れなければ負 (消去・format はまだ始めていない。
 * umount を呼ぶ前に断ったときだけ「何も書いていない」) */
static int ih_umount_hd0(KernelAPI *api)
{
    const char *dev = api->sys_is_mounted(INST_MOUNT) ? api->vfs_devname(INST_MOUNT) : 0;
    int rc;

    if (!dev || !target_streq(dev, INST_DEV)) {
        api->kprintf(ATTR_RED, "%s",
                     "Refused: /hd0 no longer holds hd0. Nothing was written.\n");
        return HDPREP_E_STILL_MOUNTED;
    }
    rc = api->sys_umount_checked(INST_MOUNT);
    if (rc < 0) {
        api->kprintf(ATTR_RED, "Refused: umount /hd0 failed (rc=%d).\n", rc);
        ih_refused_after_umount(api);
        return rc;
    }
    return 0;
}

/* LBA 0 と LBA 1 の要約 (写真で何が入っていたか分かるように、区画の項目は
 * mid・sid・名前・開始と終了の C/H/S を生のまま、範囲は BIOS 幾何で) */
static void ih_show_disk(KernelAPI *api, const InstTarget *t, u32 total)
{
    PC98PartEntry e;
    unsigned long st, ln;
    char name[PC98PT_NAME_LEN + 1];
    int i, k, used, rc;

    api->kprintf(ATTR_WHITE,
                 "  hd0 LBA 0: first bytes %02x %02x %02x %02x, boot signature 55AA: %s\n",
                 (u32)ih_lba0[0], (u32)ih_lba0[1], (u32)ih_lba0[2], (u32)ih_lba0[3],
                 (ih_lba0[510] == 0x55 && ih_lba0[511] == 0xAA) ? "yes" : "no");
    used = pc98pt_count_used(ih_lba1);
    api->kprintf(ATTR_WHITE, "  hd0 LBA 1 (partition table): %d entr%s\n",
                 used, used == 1 ? "y" : "ies");
    for (i = 0; i < PC98PT_MAX_ENTRIES; i++) {
        (void)pc98pt_get(ih_lba1, i, &e);
        if (pc98pt_entry_empty(&e)) continue;
        for (k = 0; k < PC98PT_NAME_LEN; k++) {
            u8 c = ih_lba1[i * PC98PT_ENTRY_SIZE + PC98PT_OFF_NAME + k];
            name[k] = (c >= 0x20 && c <= 0x7E) ? (char)c : '.';
        }
        name[PC98PT_NAME_LEN] = '\0';
        api->kprintf(ATTR_WHITE, "    #%d mid %02x sid %02x name \"%s\"\n",
                     i, (u32)e.bootable, (u32)e.sys_id, name);
        api->kprintf(ATTR_WHITE,
                     "       cyl %u..%u (start C/H/S %u/%u/%u, end C/H/S %u/%u/%u)\n",
                     (u32)e.start_cyl, (u32)e.end_cyl,
                     (u32)e.start_cyl, (u32)e.start_head, (u32)e.start_sector,
                     (u32)e.end_cyl, (u32)e.end_head, (u32)e.end_sector);
        rc = pc98pt_entry_range(&e, t->plan.heads, t->plan.spt, total, &st, &ln);
        if (rc == PC98PT_OK)
            api->kprintf(ATTR_WHITE, "       LBA %u, %u sectors (%u MB)\n",
                         (u32)st, (u32)ln, (u32)(ln / HDPREP_SECTORS_PER_MB));
        else
            api->kprintf(ATTR_WHITE, "       not a valid range at %u heads x %u sectors (%d)\n",
                         (u32)t->plan.heads, (u32)t->plan.spt, rc);
    }
}

/* 断る理由のうち、ERASE で空のディスクにできるもの */
static int ih_erasable(int code)
{
    return code == INST_E_FOREIGN || code == INST_E_MULTI || code == HDPREP_E_MBR_SIG ||
           code == INST_E_BROKEN || code == INST_E_START;
}

/* LBA 0 → LBA 1 をゼロで埋め、それぞれ読み戻して全部 0 を確かめる
 * (ih_write_verify が 512 B を比べる)。0 の LBA 0/1 は inst_classify で
 * 空のディスク (項目 0、55AA 無し)。マウントの検査 (inst_hdd_release) の後 */
static int ih_erase(KernelAPI *api, InstTarget *t)
{
    int rc, i;

    ih_erased = 1;
    t->erased = 1;
    for (i = 0; i < IH_SECT; i++) ih_sect[i] = 0;
    rc = ih_write_verify(api, 0, ih_sect);
    if (rc == 0) rc = ih_write_verify(api, PC98PT_LBA, ih_sect);
    if (rc != 0) {
        api->kprintf(ATTR_RED, "INCOMPLETE: erasing LBA 0 and 1 of hd0 failed (rc=%d)\n", rc);
        api->kprintf(ATTR_YELLOW, "%s",
                     "  hd0's partition table may be partly erased. Run the installer again,\n"
                     "  answer y and type ERASE again.\n");
        return rc < 0 ? rc : -1;
    }
    api->kprintf(ATTR_GREEN, "%s",
                 "  LBA 0 and 1 of hd0 erased and verified (all zero). hd0 is now an empty disk.\n");
    return 0;
}

int platform_install_target(KernelAPI *api, InstTarget *t)
{
    HddGeom hg;
    HdprepGeom geometry;
    int rc;

    ih_erased = 0;
    ih_pt_written = 0;
    rc = api->hdd_geom_info(INST_DRIVE, &hg);
    if (rc != 0) {
        api->kprintf(ATTR_RED, "  hdd_geom_info(hd0) = %d\n", rc);
        target_refuse(api, INST_E_GEOM);
        return INST_E_GEOM;
    }
    platform_install_geometry(&hg, &geometry);
    api->kprintf(ATTR_WHITE,
                 "  hd0 BIOS (AH=84h): valid=%d C/H/S=%u/%u/%u len=%u\n",
                 (int)hg.bios_valid, (u32)hg.bios_cyl, (u32)hg.bios_heads,
                 (u32)hg.bios_spt, (u32)hg.bios_seclen);
    api->kprintf(ATTR_WHITE, "  hd0 ATA: present=%d total=%u sectors (%u MB) io=%d\n",
                 (int)hg.ata_present, hg.ata_total,
                 hg.ata_total / HDPREP_SECTORS_PER_MB, (int)hg.addr_mode);

    rc = hdprep_check_geom(&geometry);
    if (rc != HDPREP_OK) { target_refuse(api, rc); return rc; }
    rc = hdprep_plan(&geometry, INST_PART_MB, &t->plan);
    if (rc != HDPREP_OK) { target_refuse(api, rc); return rc; }

    rc = api->ide_read_sector(INST_DRIVE, 0, ih_lba0);
    if (rc == 0) rc = api->ide_read_sector(INST_DRIVE, PC98PT_LBA, ih_lba1);
    if (rc != 0) {
        api->kprintf(ATTR_RED,
                     "Refused: cannot read LBA 0/1 of hd0 (rc=%d). Nothing was written.\n", rc);
        return rc < 0 ? rc : INST_E_ARG;
    }
    rc = inst_classify(ih_lba0, ih_lba1, t->plan.heads, t->plan.spt,
                       geometry.ata_total, t->plan.start, &t->mode);
    if (rc != 0) {
        if (!ih_erasable(rc)) { target_refuse(api, rc); return rc; }
        /* 消す道 (N4 の例外): まだ消さない。要約を出し、消した後の空のディスクと
         * して残りの検査 (マウント・大きさ・容量) と確認画面まで進む。消すのは
         * y と ERASE の後 (inst_hdd_release) */
        api->kprintf(ATTR_RED, "hd0 cannot be used as it is: %s (code %d)\n",
                     inst_reason(rc), rc);
        if (rc == INST_E_FOREIGN || rc == INST_E_MULTI || rc == HDPREP_E_MBR_SIG)
            ih_foreign_hint(api);
        else
            ih_host_hint(api);           /* OS32 の項目が中途半端 (開始違い・壊れ) */
        api->kprintf(ATTR_CYAN, "%s", "Current contents of hd0:\n");
        ih_show_disk(api, t, geometry.ata_total);
        api->kprintf(ATTR_YELLOW, "%s",
                     "  The installer can erase the whole partition table of hd0 (LBA 0 and 1)\n"
                     "  and install onto it as an empty disk. Nothing is erased unless you\n"
                     "  answer y at the confirmation below and then type ERASE.\n");
        t->erase_needed = 1;
        t->erase_code = rc;
        t->mode = INST_MODE_EMPTY;
    }
    return 0;
}

int platform_install_check_media(KernelAPI *api, u32 ipl_len, u32 loader_len, u32 kernel_len)
{
    int rc = inst_check_boot_files(ipl_len, loader_len, kernel_len);
    if (rc != 0) {
        api->kprintf(ATTR_WHITE, "  sizes: IPL %u, loader %u, vmkernel.lz4 %u bytes\n",
                     ipl_len, loader_len, kernel_len);
        target_refuse(api, rc);
        return rc;
    }
    return 0;
}

void inst_hdd_describe(KernelAPI *api, const InstTarget *t)
{
    const HdprepPlan *p = &t->plan;

    api->kprintf(ATTR_CYAN, "Target: hd0 (IDE drive 0 = BIOS DA 80h), %s\n",
                 inst_mode_name(t->mode));
    api->kprintf(ATTR_WHITE,
                 "  OS32 area: LBA %u..%u (%u MB), cyl %u..%u at %u heads x %u sectors\n",
                 p->start, p->start + p->len - 1UL, p->len / HDPREP_SECTORS_PER_MB,
                 p->start / p->cyl_sectors, p->end_cyl, p->heads, p->spt);
    api->kprintf(ATTR_WHITE, "%s",
                 "  Writes: ext2 in the area, partition table (LBA 1), "
                 "loader (LBA 2..17), IPL (LBA 0)\n");
    if (t->erase_needed) {
        api->kprintf(ATTR_RED,
                     "  hd0 holds another system's partitions or a table OS32 cannot use\n"
                     "  (%s, code %d).\n", inst_reason(t->erase_code), t->erase_code);
        api->kprintf(ATTR_RED, "%s",
                     "  After y, type ERASE to erase the partition table (LBA 0 and 1) and\n"
                     "  install onto hd0 as an empty disk. EVERYTHING ON hd0 WILL BE LOST.\n"
                     "  Anything but ERASE writes nothing.\n");
    }
    if (t->mode != INST_MODE_EMPTY)
        api->kprintf(ATTR_RED, "%s",
                     "  The existing OS32 area (the temporary storage, /hd0) is "
                     "re-created: ALL FILES IN IT WILL BE LOST.\n");
    if (t->umount_hd0)
        api->kprintf(ATTR_YELLOW, "%s", "  hd0 is mounted at /hd0 and will be unmounted first.\n");
}

int inst_hdd_release(KernelAPI *api, InstTarget *t)
{
    int rc, unmounted = 0;

    /* ---- 使用中の検査 (N6): 消去・format の前の最後の検査。失敗なら消さず
     * format もしない (umount を呼ぶ前なら何も書いていない。呼んだ後は umount の
     * sync が書き出し得る: ih_refused_after_umount)。ERASE の打鍵を待つ間に
     * マウントが増えた・/hd0 の相手が替わったのもここで分かる ---- */
    if (t->umount_hd0) {
        rc = ih_umount_hd0(api);
        if (rc != 0) return rc;
        t->umount_hd0 = 0;
        unmounted = 1;
    }
    if (api->dev_mount_count(INST_DRIVE) != 0) {
        if (unmounted) {
            api->kprintf(ATTR_RED, "Refused: %s (code %d).\n",
                         inst_reason(HDPREP_E_STILL_MOUNTED), HDPREP_E_STILL_MOUNTED);
            ih_refused_after_umount(api);
        } else {
            target_refuse(api, HDPREP_E_STILL_MOUNTED);
        }
        return HDPREP_E_STILL_MOUNTED;
    }
    t->mounts = 0;
    /* ---- 消す (N4 の例外): 全検査・y・ERASE・マウントの検査の後。インストーラ
     * 自身の最初の書き込み (その前に書き得るのは umount の sync だけ) ---- */
    if (t->erase_needed) return ih_erase(api, t);
    return 0;
}

int platform_install_partition(KernelAPI *api, const InstTarget *t)
{
    int rc;

    /* ---- 区画表 → 読み戻し比較 ---- */
    rc = inst_build_pt(ih_sect, &t->plan);
    if (rc != 0) {
        inst_hdd_incomplete(api, "cannot build the partition entry", rc);
        return rc < 0 ? rc : -1;
    }
    ih_pt_written = 1;
    rc = api->ide_write_sector(INST_DRIVE, PC98PT_LBA, ih_sect);
    if (rc == 0) rc = api->ide_read_sector(INST_DRIVE, PC98PT_LBA, ih_back);
    if (rc != 0 || !ih_memeq(ih_sect, ih_back, IH_SECT)) {
        inst_hdd_incomplete(api, "partition table write/readback failed", rc);
        /* 表が中途半端なら次の実行は「OS32 の項目ではない」と断る */
        api->kprintf(ATTR_YELLOW, "%s",
                     "  If the next run finds hd0's partition table unusable, answer y and\n"
                     "  type ERASE at its prompt to clear it (the guest cannot repair the\n"
                     "  table), or:\n");
        ih_host_hint(api);
        return rc < 0 ? rc : -1;
    }
    api->kprintf(ATTR_GREEN, "%s", "  Partition table written and verified.\n");

    return 0;
}

int platform_install_write_boot(KernelAPI *api, const InstTarget *t,
                        const u8 *ipl, u32 ipl_len,
                        const u8 *loader, u32 loader_len)
{
    u32 off, n, i;
    int rc;

    /* 事前検査を通っていること (ここでも範囲を守る) */
    if (inst_check_boot_files(ipl_len, loader_len, 1) != 0 || !ipl || !loader) {
        inst_hdd_incomplete(api, "boot files are not the checked ones", INST_E_ARG);
        return INST_E_ARG;
    }

    /* ---- ローダ → LBA 2〜 (512 B ずつ、端数は 0 で埋める) ---- */
    for (off = 0; off < loader_len; off += IH_SECT) {
        n = loader_len - off;
        if (n > IH_SECT) n = IH_SECT;
        for (i = 0; i < IH_SECT; i++) ih_sect[i] = (i < n) ? loader[off + i] : 0;
        rc = ih_write_verify(api, INST_LOADER_LBA + off / IH_SECT, ih_sect);
        if (rc != 0) {
            inst_hdd_incomplete(api, "loader write/readback failed", rc);
            return rc < 0 ? rc : -1;
        }
    }

    /* ---- IPL → LBA 0 (BIOS 幾何を [8]/[9] に、末尾 55AA) ---- */
    for (i = 0; i < IH_SECT; i++) ih_sect[i] = (i < ipl_len) ? ipl[i] : 0;
    inst_patch_ipl(ih_sect, t->plan.heads, t->plan.spt);
    rc = ih_write_verify(api, 0, ih_sect);
    if (rc != 0) {
        inst_hdd_incomplete(api, "IPL write/readback failed", rc);
        return rc < 0 ? rc : -1;
    }
    api->kprintf(ATTR_GREEN, "  IPL (geometry %u/%u) and loader (%u bytes) written and verified.\n",
                 t->plan.heads, t->plan.spt, loader_len);
    return 0;
}
