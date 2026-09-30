/* ======================================================================== */
/*  INST_HDD.C — インストーラの hd0 の検査と書き込み (cdinst / install 共通)  */
/*                                                                          */
/*  手順と規則は inst_hdd.h / inst_disk.h。「全検査 → 書く」の手本は         */
/*  userland/shell/cmd_hdprep.c。                                            */
/* ======================================================================== */

#include "inst_hdd.h"
#include "drivers/pc98pt.h"

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

static int ih_streq(const char *a, const char *b)
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
static void ih_refuse(KernelAPI *api, int code)
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

static void ih_geom_from_kapi(const HddGeom *hg, HdprepGeom *g)
{
    g->ata_present   = hg->ata_present;
    g->addr_mode     = hg->addr_mode;
    g->ata_total     = hg->ata_total;
    g->ata_cur_cyl   = hg->ata_cur_cyl;
    g->ata_cur_heads = hg->ata_cur_heads;
    g->ata_cur_spt   = hg->ata_cur_spt;
    g->bios_queried  = hg->bios_queried;
    g->bios_valid    = hg->bios_valid;
    g->bios_cyl      = hg->bios_cyl;
    g->bios_heads    = hg->bios_heads;
    g->bios_spt      = hg->bios_spt;
    g->bios_seclen   = hg->bios_seclen;
}

/* 1 セクタを書いて読み戻して比べる */
static int ih_write_verify(KernelAPI *api, u32 lba, const u8 *buf)
{
    int rc = api->ide_write_sector(INST_DRIVE, lba, buf);
    if (rc == 0) rc = api->ide_read_sector(INST_DRIVE, lba, ih_back);
    if (rc != 0) return rc;
    return ih_memeq(buf, ih_back, IH_SECT) ? 0 : -1;
}

/* マウントの検査 (N6): ルートの hd0 は断る。外すのは /hd0 に hd0 がマウント
 * されているときだけ。別の prefix にもマウントされていれば、承認を取る前に
 * 断る (外す相手を取り違えない) */
static int ih_check_mounts(KernelAPI *api, InstTarget *t)
{
    const char *rootdev;
    int rc, root_hd0;

    rootdev = api->vfs_devname("/");
    root_hd0 = (rootdev && ih_streq(rootdev, INST_DEV)) ? 1 : 0;
    t->mounts = api->dev_mount_count(INST_DRIVE);
    rc = hdprep_check_mounts(t->mounts, root_hd0);
    if (rc < 0) { ih_refuse(api, rc); return rc; }
    t->umount_hd0 = 0;
    if (t->mounts > 0) {
        const char *dev = api->sys_is_mounted(INST_MOUNT) ? api->vfs_devname(INST_MOUNT) : 0;
        if (t->mounts == 1 && dev && ih_streq(dev, INST_DEV)) {
            t->umount_hd0 = 1;
        } else {
            api->kprintf(ATTR_RED,
                         "Refused: hd0 is mounted somewhere other than /hd0 (%d mount(s)). "
                         "Unmount it first. Nothing was written.\n", t->mounts);
            return HDPREP_E_STILL_MOUNTED;
        }
    }
    return 0;
}

/* umount を呼んだ後に断る: 消去も format も始めていないが、「何も書いていない」
 * とは言えない。umount (fs/vfs.c vfs_umount) は外す前に ops->sync() を呼び、
 * ext2 の dirty なメタデータ (スーパーブロック・グループ記述子) を書き出し得る
 * (失敗した umount でも、書き出しの途中で失敗した可能性がある) */
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

    if (!dev || !ih_streq(dev, INST_DEV)) {
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

/* ---- 鍵 --------------------------------------------------------------- */

/* CR を返した直後か (CRLF の LF を捨てる)、覗いて戻した 1 バイト (-1 = 無し)。
 * inst_hdd_check が毎回戻す */
static int ih_last_cr;
static int ih_pushed = -1;

/* 非ブロックで 1 バイト。kbd → serial。-1 = 入力なし。実物のドライバは受けた
 * NUL を 0 で返すので、0 以上はすべて入力 (0 を「無し」と読むと NUL を含む
 * 行が ERASE に化ける) */
static int ih_poll(KernelAPI *api)
{
    int ch;
    if (ih_pushed >= 0) { ch = ih_pushed; ih_pushed = -1; return ch; }
    ch = api->kbd_trygetchar();
    if (ch < 0) ch = api->serial_trygetchar();
    return ch;
}

/* ih_poll + CRLF: CR の直後の LF は行末の一部として捨てる (-1 を返す) */
static int ih_poll_key(KernelAPI *api)
{
    int ch = ih_poll(api);
    if (ch < 0) return -1;
    if (ch == '\n' && ih_last_cr) { ih_last_cr = 0; return -1; }
    ih_last_cr = (ch == '\r') ? 1 : 0;
    return ch;
}

int inst_hdd_getkey(KernelAPI *api)
{
    int ch;
    do { ch = ih_poll_key(api); } while (ch < 0);
    return ch;
}

/* 1 字で決まる問いの直後の問い (cdinst の [0-3] の後の y/N) 用。最初の 1 字が
 * 行末 (CR / LF / CRLF) なら前の答えの行末として 1 回だけ捨てて次を返す
 * (端末の「1 + Enter」の Enter が y/N の答え = 取り消しにならない)。2 つめの
 * 行末はそのまま返す (= 取り消し)。NUL・ESC・その他の字は捨てない。
 * 規則は ERASE の行 (ih_read_line の skip_eol) と同じ: 届いている分も後から
 * 届く分も同じ扱いで、到着の時刻に左右されない */
int inst_hdd_getkey_after_key(KernelAPI *api)
{
    int ch = inst_hdd_getkey(api);
    if (ch == '\r' || ch == '\n') ch = inst_hdd_getkey(api);   /* CRLF の LF は捨て済み */
    return ch;
}

/* 1 行読む (CR か LF で終わり)。表示できる ASCII だけをそのまま受け、
 * それ以外 (NUL・BS・ESC などの制御文字) や長すぎる行は「一致しない行」にする。
 * ESC はその場で打ち切る。CR の後にもう届いている LF は同じ行末として捨てる
 * (後から届く LF は ih_poll_key が捨てる)。
 * skip_eol = 1: 最初の 1 字が行末 (CR / LF / CRLF) なら、それは前の問いの答えの
 * 行末として映さずに 1 回だけ読み捨てる (inst_hdd_ask_erase の注釈)。
 * 戻り値: 0 = buf に行 / -1 = 一致しない行 */
static int ih_read_line(KernelAPI *api, char *buf, int max, int skip_eol)
{
    int n = 0, bad = 0, ch;

    for (;;) {
        ch = inst_hdd_getkey(api);
        if (skip_eol) {
            skip_eol = 0;                /* 最初の 1 字だけ */
            if (ch == '\r' || ch == '\n') continue;   /* CRLF の LF は ih_poll_key が捨てる */
        }
        if (ch == '\r' || ch == '\n') break;
        if (ch == 0x1B) { bad = 1; break; }
        if (ch < 0x20 || ch > 0x7E) { bad = 1; continue; }
        api->kprintf(ATTR_WHITE, "%c", ch);
        if (n < max - 1) buf[n++] = (char)ch;
        else bad = 1;
    }
    if (ch == '\r') {
        ch = ih_poll_key(api);           /* 届いている LF なら捨てる */
        if (ch >= 0) ih_pushed = ch;     /* 別の字なら戻す */
    }
    buf[n] = '\0';
    api->kprintf(ATTR_WHITE, "%s", "\n");
    return bad ? -1 : 0;
}

/* LBA 0 と LBA 1 の要約 (写真で何が入っていたか分かるように、区画の項目は
 * mid・sid・名前・開始と終了の C/H/S を生のまま、範囲は BIOS 幾何で) */
static void ih_show_disk(KernelAPI *api, const InstTarget *t)
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
        rc = pc98pt_entry_range(&e, t->plan.heads, t->plan.spt, t->g.ata_total, &st, &ln);
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

int inst_hdd_ask_erase(KernelAPI *api, const InstTarget *t)
{
    static char line[INST_LINE_MAX];

    if (!t->erase_needed) return 0;
    api->kprintf(ATTR_YELLOW, "%s",
                 "  hd0's whole partition table (LBA 0 and 1) will be erased first.\n"
                 "  EVERYTHING ON hd0 WILL BE LOST, including the partitions of other\n"
                 "  systems. Type ERASE (capital letters) and press Enter to erase it.\n"
                 "  Anything else leaves hd0 as it is.\n");
    api->kprintf(ATTR_YELLOW, "%s", "Erase hd0's partition table? Type ERASE: ");
    /* y/N は 1 字で決まるので、y の後に打った Enter (端末が y と一緒に送る
     * CR / LF / CRLF) がまだ残っている。それを ERASE の空行と読むと、
     * 「y + Enter → ERASE + Enter」が必ず取り消しになる (シリアル端末の行送信)。
     * 規則: y の後の最初の行末 1 つは y の行末とみなして読み捨てる。もう届いて
     * いても (y\r\n を一度に受けた)、後から届いても (人が y の後で Enter を押した)
     * 同じ扱い — 「届いている分だけ読み捨て、後から届いた分は ERASE の行の先頭の
     * 空行として 1 回だけ無視する」のと結果は同じで、到着の時刻に左右されない。
     * 行末以外 (E・NUL・ESC など) が先に来れば何も捨てない (NUL は入力のまま)。
     * 2 つめの行末は ERASE の行の空行 = 取り消し (Enter だけで取り消すには、
     * y の後に Enter を押していなければ 2 回押すことになる: 安全側) */
    if (ih_read_line(api, line, INST_LINE_MAX, 1) != 0 || !ih_streq(line, INST_ERASE_WORD)) {
        api->kprintf(ATTR_WHITE, "%s", "Not erased. Nothing was written.\n");
        return t->erase_code;
    }
    return 0;
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

int inst_hdd_check(KernelAPI *api, InstTarget *t)
{
    int rc;

    ih_erased = 0;
    ih_pt_written = 0;
    ih_last_cr = 0;
    ih_pushed = -1;
    t->erased = 0;
    t->erase_needed = 0;
    t->erase_code = 0;
    t->umount_hd0 = 0;

    rc = api->hdd_geom_info(INST_DRIVE, &t->hg);
    if (rc != 0) {
        api->kprintf(ATTR_RED, "  hdd_geom_info(hd0) = %d\n", rc);
        ih_refuse(api, INST_E_GEOM);
        return INST_E_GEOM;
    }
    ih_geom_from_kapi(&t->hg, &t->g);
    api->kprintf(ATTR_WHITE,
                 "  hd0 BIOS (AH=84h): valid=%d C/H/S=%u/%u/%u len=%u\n",
                 (int)t->hg.bios_valid, (u32)t->hg.bios_cyl, (u32)t->hg.bios_heads,
                 (u32)t->hg.bios_spt, (u32)t->hg.bios_seclen);
    api->kprintf(ATTR_WHITE, "  hd0 ATA: present=%d total=%u sectors (%u MB) io=%d\n",
                 (int)t->hg.ata_present, t->hg.ata_total,
                 t->hg.ata_total / HDPREP_SECTORS_PER_MB, (int)t->hg.addr_mode);

    rc = hdprep_check_geom(&t->g);
    if (rc != HDPREP_OK) { ih_refuse(api, rc); return rc; }
    rc = hdprep_plan(&t->g, INST_PART_MB, &t->plan);
    if (rc != HDPREP_OK) { ih_refuse(api, rc); return rc; }

    rc = api->ide_read_sector(INST_DRIVE, 0, ih_lba0);
    if (rc == 0) rc = api->ide_read_sector(INST_DRIVE, PC98PT_LBA, ih_lba1);
    if (rc != 0) {
        api->kprintf(ATTR_RED,
                     "Refused: cannot read LBA 0/1 of hd0 (rc=%d). Nothing was written.\n", rc);
        return rc < 0 ? rc : INST_E_ARG;
    }
    rc = inst_classify(ih_lba0, ih_lba1, t->plan.heads, t->plan.spt,
                       t->g.ata_total, t->plan.start, &t->mode);
    if (rc != 0) {
        if (!ih_erasable(rc)) { ih_refuse(api, rc); return rc; }
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
        ih_show_disk(api, t);
        api->kprintf(ATTR_YELLOW, "%s",
                     "  The installer can erase the whole partition table of hd0 (LBA 0 and 1)\n"
                     "  and install onto it as an empty disk. Nothing is erased unless you\n"
                     "  answer y at the confirmation below and then type ERASE.\n");
        t->erase_needed = 1;
        t->erase_code = rc;
        t->mode = INST_MODE_EMPTY;
    }
    return ih_check_mounts(api, t);
}

int inst_hdd_check_media(KernelAPI *api, const InstTarget *t,
                         u32 ipl_len, u32 loader_len, u32 kernel_len,
                         const InstNeed *need)
{
    InstRoom room;
    int rc;

    rc = inst_check_boot_files(ipl_len, loader_len, kernel_len);
    if (rc != 0) {
        api->kprintf(ATTR_WHITE, "  sizes: IPL %u, loader %u, vmkernel.lz4 %u bytes\n",
                     ipl_len, loader_len, kernel_len);
        ih_refuse(api, rc);
        return rc;
    }
    rc = inst_check_space(t->plan.len, need, &room);   /* 失敗でも room は埋まる */
    api->kprintf(ATTR_WHITE,
                 "  space: need %u KiB / %u inodes, the area has %u KiB / %u inodes free\n",
                 room.need_blocks, room.need_inodes, room.free_blocks, room.free_inodes);
    if (rc != 0) { ih_refuse(api, rc); return rc; }
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
            ih_refuse(api, HDPREP_E_STILL_MOUNTED);
        }
        return HDPREP_E_STILL_MOUNTED;
    }
    t->mounts = 0;
    /* ---- 消す (N4 の例外): 全検査・y・ERASE・マウントの検査の後。インストーラ
     * 自身の最初の書き込み (その前に書き得るのは umount の sync だけ) ---- */
    if (t->erase_needed) return ih_erase(api, t);
    return 0;
}

int inst_hdd_prepare(KernelAPI *api, const InstTarget *t)
{
    int rc;

    /* ---- ext2 (区画表を読まずに範囲だけ) ---- */
    api->kprintf(ATTR_CYAN, "  Formatting ext2 (LBA %u, %u sectors)...\n",
                 t->plan.start, t->plan.len);
    rc = api->ext2_format_at(INST_DRIVE, t->plan.start, t->plan.len);
    if (rc != 0) {
        inst_hdd_incomplete(api, "ext2_format_at failed (partition table not written)", rc);
        return rc < 0 ? rc : -1;
    }

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

    /* ---- 通常のマウントで確認 (区画表 → ext2_find_partition → ext2) ---- */
    rc = api->sys_mount(INST_MOUNT, INST_DEV, "ext2");
    if (rc != 0) {
        inst_hdd_incomplete(api, "mount /hd0 through the new partition table failed", rc);
        return rc < 0 ? rc : -1;
    }
    api->kprintf(ATTR_GREEN, "%s", "  Mounted /hd0.\n");
    return 0;
}

int inst_hdd_write_boot(KernelAPI *api, const InstTarget *t,
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
