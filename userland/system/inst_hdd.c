/* ======================================================================== */
/*  INST_HDD.C — インストーラの hd0 の検査と書き込み (cdinst / install 共通)  */
/*                                                                          */
/*  手順と規則は inst_hdd.h / inst_disk.h。「全検査 → 書く」の手本は         */
/*  userland/shell/cmd_hdprep.c。                                            */
/* ======================================================================== */

#include "inst_hdd.h"
#include "include/platform_install_boot.h"

static int ih_streq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

/* 書く前に断る (umount も呼んでいないので「何も書いていない」と言える) */
static void ih_refuse(KernelAPI *api, int code)
{
    api->kprintf(ATTR_RED, "Refused: %s (code %d). Nothing was written.\n",
                 inst_reason(code), code);
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
    t->mounts = api->dev_mount_count(t->plan.drive);
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

int inst_hdd_check(KernelAPI *api, InstTarget *t)
{
    ih_last_cr = 0;
    ih_pushed = -1;
    t->erased = 0;
    t->erase_needed = 0;
    t->erase_code = 0;
    t->umount_hd0 = 0;

    /* Zero sectors queries authorization without touching the device. Refuse
     * before umount/sync, ERASE, formatting or any installer file writes. */
    if (api->ide_write_sectors(INST_DRIVE, 0, 0, NULL) != 0) {
        api->kprintf(ATTR_RED, "%s",
                     "Disk write not authorized. Nothing was written.\n"
                     "CUI の前景で /sbin の install または cdinst として起動してください。\n");
        return -1;
    }

    int rc = platform_install_target(api, t);
    if (rc != 0) return rc;
    return ih_check_mounts(api, t);
}

int inst_hdd_check_media(KernelAPI *api, const InstTarget *t,
                         u32 ipl_len, u32 loader_len, u32 kernel_len,
                         const InstNeed *need)
{
    InstRoom room;
    int rc;

    rc = platform_install_check_media(api, ipl_len, loader_len, kernel_len);
    if (rc != 0) return rc;
    rc = inst_check_space(t->plan.len, need, &room);   /* 失敗でも room は埋まる */
    api->kprintf(ATTR_WHITE,
                 "  space: need %u KiB / %u inodes, the area has %u KiB / %u inodes free\n",
                 room.need_blocks, room.need_inodes, room.free_blocks, room.free_inodes);
    if (rc != 0) { ih_refuse(api, rc); return rc; }
    return 0;
}

int inst_hdd_prepare(KernelAPI *api, const InstTarget *t)
{
    int rc;

    /* ---- ext2 (区画表を読まずに範囲だけ) ---- */
    api->kprintf(ATTR_CYAN, "  Formatting ext2 (LBA %u, %u sectors)...\n",
                 t->plan.start, t->plan.len);
    rc = api->ext2_format_at(t->plan.drive, t->plan.start, t->plan.len);
    if (rc != 0) {
        inst_hdd_incomplete(api, "ext2_format_at failed (partition table not written)", rc);
        return rc < 0 ? rc : -1;
    }

    rc = platform_install_partition(api, t);
    if (rc != 0) return rc;

    /* ---- 通常のマウントで確認 (区画表 → ext2_find_partition → ext2) ---- */
    rc = api->sys_mount(INST_MOUNT, INST_DEV, "ext2");
    if (rc != 0) {
        inst_hdd_incomplete(api, "mount /hd0 through the new partition table failed", rc);
        return rc < 0 ? rc : -1;
    }
    api->kprintf(ATTR_GREEN, "%s", "  Mounted /hd0.\n");
    return 0;
}
