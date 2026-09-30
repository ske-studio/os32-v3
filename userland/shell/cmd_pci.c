/* ======================================================================== */
/*  PCI モジュール (cmd_pci.c) — `lspci` と `pcidump`                       */
/*                                                                          */
/*  実機 PC-9821Ra266 の内蔵 LAN (Intel 82557 = 8086:1229) を見つけ、BAR と */
/*  Interrupt Line を読み出すための口。**NP21/W には PCI が無い**ので、     */
/*  エミュレータでは「PCI 無し」を正しく報告することだけが仕事になる。      */
/*                                                                          */
/*  `pcidump` は config 256 バイトの生ダンプ — 票 §5-2 の R2 / R3 / R4      */
/*  (82557 の Command と Subsystem、C バスブリッヂの PIRQ 割り当て、        */
/*  PCMC の vendor:device) を実機の 1 回でまとめて持ち帰るための道具。      */
/*                                                                          */
/*  票  : docs/tasks/realhw/TASK_LAN_82557.md §2 L-A / §5-2                 */
/*  復号: drivers/pci_decode.c を**写さずに**リンクしている                  */
/*        (build/programs.mk の PCI_DECODE_USER_OBJ)。BAR の種別と番地・    */
/*        クラス名・Header Type の切り出しがカーネルと食い違わないため。    */
/* ======================================================================== */

#include "shell.h"
#include "drivers/pci_decode.h"   /* PROGRAM_FLAGS の -I. で引く */
#include "pci_verbose.h"          /* `lspci -v` の行づくり (純粋関数) */

/* P-1: drivers/pci.h の struct pci_dev と**同じ並び**でなければならない。
 * `pci_get` はカーネル側の定義 (i386 で 40 バイト) で書くので、ここが
 * 違うと呼び手のスタックを踏む (drivers/ide.h の IdeInfo が 92B/96B で
 * ずれて `ide 0` が壊れたのと同じ穴)。drivers/pci.h はカーネル内部
 * ヘッダなので写しをここに置く — 向こうを変えたら必ず一緒に直すこと。
 * カーネル側は drivers/pci.c の STATIC_ASSERT が 40 バイトを見張る。 */
typedef struct {
    u8  bus;            /*  0 */
    u8  dev;            /*  1 */
    u8  fn;             /*  2 */
    /*  3: padding */
    u16 vendor;         /*  4 */
    u16 device;         /*  6 */
    u8  cls;            /*  8  config 0x0B (カーネル側の名前は class) */
    u8  subclass;       /*  9  config 0x0A */
    u8  progif;         /* 10  config 0x09 */
    u8  header;         /* 11  config 0x0E */
    u32 bar[PCI_CFG_BAR_COUNT]; /* 12〜35 */
    u8  irq_line;       /* 36  config 0x3C */
    u8  irq_pin;        /* 37  config 0x3D */
    u16 command;        /* 38  config 0x04 */
} PciDev;               /* 40 バイト */

/* 写しが本物と同じ大きさであることをコンパイル時に確かめる
 * (カーネル側は drivers/pci.c の同名の検査)。ずれたらここで止まる。 */
STATIC_ASSERT(sizeof(PciDev) == 40, shell_pci_dev_is_40);

/* P-2: drivers/pci_bind.h の `struct pci_bind_info` と**同じ並び** (8 バイト)。
 * `pci_bind_info` (KAPI v60) はカーネル側の定義で 8 バイトちょうど書くので、
 * ここが違うと呼び手のスタックを踏む (P-1 の PciDev と同じ理由)。
 * drivers/pci_bind.h はカーネル内部ヘッダなので写しをここに置く —
 * 向こうを変えたら必ず一緒に直すこと。カーネル側は drivers/pci_bind.c の
 * STATIC_ASSERT が 8 バイトを見張る。
 * 票: docs/tasks/v3/TASK_HAL_WIRING.md §1-4 (診断の取得口) */
typedef struct {
    u8 bus;            /* 0 */
    u8 dev;            /* 1 */
    u8 fn;             /* 2 */
    u8 result;         /* 3  PCI_BIND_NONE / BOUND / DECLINED / QUARANTINED */
    u8 irq;            /* 4  列挙時の irq_line (0xFF = 未割り当て) */
    u8 reason;         /* 5  PCI_BIND_OK / NO_DRIVER / ... */
    u8 line_state;     /* 6  PCI_LINE_OK / STORM_MASKED / QUARANTINED */
    u8 pad;            /* 7  常に 0 */
} PciBindInfo;         /* 8 バイト */

STATIC_ASSERT(sizeof(PciBindInfo) == 8, shell_pci_bind_info_is_8);

#define SH_BIND_NONE        0
#define SH_BIND_BOUND       1
#define SH_BIND_DECLINED    2
#define SH_BIND_QUARANTINED 3

#define SH_LINE_OK           0
#define SH_LINE_STORM_MASKED 1
#define SH_LINE_QUARANTINED  2

/* reason の短い名前。**添字は drivers/pci_bind.h の PCI_BIND_* の値**
 * (OK 0 / NO_DRIVER 1 / IRQ_UNSUPPORTED 2 / IRQ_QUARANTINED 3 /
 *  RESET_FAILED 4 / START_FAILED 5 / NOISY 6 / NOISY_UNMASKABLE 7 /
 *  DECLINED_UNSPECIFIED 8)。switch ではなく表にするのは、常駐シェルの
 * バイナリを膨らませないため (文字列は短い語だけ)。 */
static const char *const bind_reason_name[] = {
    "ok", "no-driver", "irq-unsupported", "irq-quarantined",
    "reset-failed", "start-failed", "noisy", "noisy-unmaskable",
    "unspecified"
};

static const char *bind_reason_text(u8 reason)
{
    /* 知らない値は「理由なし」ではなく unspecified に寄せる
     * (カーネルが後から reason を増やしても嘘を出さない)。 */
    if ((u32)reason >= (u32)(sizeof(bind_reason_name) /
                             sizeof(bind_reason_name[0])))
        return "unspecified";
    return bind_reason_name[reason];
}

/* 1 行の末尾に結線の注記を足す (KAPI v60)。**何も無ければ 1 文字も出さない**
 * — 一致する driver の無い装置 (result = NONE、線も正常) はふつうのこと。
 * line_state は**カーネルが読む時点で合成**するので、結線のときは正常
 * だった線が後から隔離されていれば result = bound のまま
 * `[irq N quarantined]` が付く (票 §1-4 往復 9 R1)。 */
static void pci_print_bind(int idx)
{
    PciBindInfo bi;

    if (g_api->pci_bind_info((u32)idx, &bi) != 0) return;
    if (bi.result == SH_BIND_NONE && bi.line_state == SH_LINE_OK) return;

    if (bi.result == SH_BIND_BOUND) {
        g_api->kprintf(ATTR_GREEN, "  bound (%s) irq=%u",
                       bind_reason_text(bi.reason), (u32)bi.irq);
    } else if (bi.result == SH_BIND_DECLINED) {
        g_api->kprintf(ATTR_YELLOW, "  declined (%s)",
                       bind_reason_text(bi.reason));
    } else if (bi.result == SH_BIND_QUARANTINED) {
        g_api->kprintf(ATTR_RED, "  quarantined (%s)",
                       bind_reason_text(bi.reason));
    }

    if (bi.line_state == SH_LINE_QUARANTINED) {
        g_api->kprintf(ATTR_RED, "  [irq %u quarantined]", (u32)bi.irq);
    } else if (bi.line_state == SH_LINE_STORM_MASKED) {
        g_api->kprintf(ATTR_YELLOW, "  [irq %u storm-masked]", (u32)bi.irq);
    }
}

/* Interrupt Pin の 1〜4 を A〜D に。0 = 割り込みを使わない。
 * PC-98 ではこの Pin がスロットごとに違う PIRQ 線へ配線され
 * (io_pci.md 表3)、PnP BIOS が 8259 の入力へ落とした結果が
 * Interrupt Line に入る。だから**表示するのは両方**。 */
static char pci_pin_letter(u8 pin)
{
    if (pin >= 1 && pin <= 4) return (char)('A' + (pin - 1));
    return '-';
}

/* 10 進 (先頭 0x なら 16 進) を 1 つ読む。読めなければ -1。 */
static int pci_parse_num(const char *s)
{
    long v = 0;
    int any = 0;

    if (s == 0 || *s == '\0') return -1;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        while (*s) {
            int d;
            if (*s >= '0' && *s <= '9') d = *s - '0';
            else if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
            else if (*s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
            else return -1;
            v = v * 16 + d;
            if (v > 0xFFFF) return -1;
            any = 1; s++;
        }
    } else {
        while (*s) {
            if (*s < '0' || *s > '9') return -1;
            v = v * 10 + (*s - '0');
            if (v > 0xFFFF) return -1;
            any = 1; s++;
        }
    }
    return any ? (int)v : -1;
}

/* ------------------------------------------------------------------ */
/*  lspci — 1 行 1 デバイス                                            */
/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
/*  lspci -v — 1 デバイスを複数行で (読み取り専用)                     */
/*                                                                    */
/*  config 0x00〜0x3F を `pci_cfg_read32` で DWORD 16 回読み、行の中身は */
/*  pci_verbose.c (ホスト試験あり) が作る。**書き込みは一切しない** —   */
/*  BAR の大きさを調べる 0xFFFFFFFF の書き込みもしない (BIOS の割り当て */
/*  を一瞬でも壊さない)。1 行を `"%s\n"` の 1 引数で出すので、CPL=3 の   */
/*  引数の窓 (64 バイト) に行の長さは効かない。                         */
/* ------------------------------------------------------------------ */
static void lspci_verbose_one(u32 bus, u32 dev, u32 fn)
{
    u32 cfg[PCI_VERBOSE_CFG_DWORDS];
    char line[PCI_VERBOSE_LINE_MAX];
    int k, r;

    for (k = 0; k < PCI_VERBOSE_CFG_DWORDS; k++)
        cfg[k] = g_api->pci_cfg_read32(bus, dev, fn, (u32)(k * 4));

    for (k = 0; ; k++) {
        r = pci_verbose_line(cfg, bus, dev, fn, k, line, (int)sizeof(line));
        if (r == PCI_VERBOSE_END) break;
        if (r == PCI_VERBOSE_SKIP) continue;
        g_api->kprintf((k == 0) ? ATTR_CYAN : ATTR_WHITE, "%s\n", line);
    }
}

/* `lspci -v` / `lspci -v B:D.F` / `lspci -v bus dev fn`。 */
static int cmd_lspci_verbose(int argc, char **argv)
{
    int n, i;
    int one = 0;                /* 1 = 1 台だけ (B:D.F か bus dev fn) */
    u32 bus = 0, dev = 0, fn = 0, idw;
    PciDev d;

    /* **引数を先に検査する。** PCI の有無を先に見ると、NP21/W では
     * `lspci -v 256:0.0` や `lspci -v garbage` が成功で終わり、打ち間違いが
     * 実機へ持ち越される (Codex P3)。 */
    if (argc == 3) {
        if (pci_parse_bdf(argv[2], &bus, &dev, &fn) != 0) goto usage;
        one = 1;
    } else if (argc == 5) {
        int b = pci_parse_num(argv[2]);
        int v = pci_parse_num(argv[3]);
        int f = pci_parse_num(argv[4]);
        if (b < 0 || b > (int)PCI_BUS_MASK ||
            v < 0 || v > (int)PCI_DEV_MASK ||
            f < 0 || f > (int)PCI_FN_MASK) goto usage;
        bus = (u32)b; dev = (u32)v; fn = (u32)f;
        one = 1;
    } else if (argc != 2) {
        goto usage;
    }

    n = g_api->pci_count();
    if (n <= 0) {
        g_api->kprintf(ATTR_CYAN, "%s",
                       "lspci: no PCI (mechanism #1 not present)\n");
        /* 一覧なら「0 台」は正しい答え (引数なしの lspci と同じ)。
         * 1 台を名指ししたのに読めないのは失敗 (pcidump と同じ)。 */
        return one ? SH_STATUS_ERROR : 0;
    }

    if (one) {
        idw = g_api->pci_cfg_read32(bus, dev, fn, PCI_CFG_VENDOR_ID);
        if (pci_extract16(idw, PCI_CFG_VENDOR_ID) == PCI_VENDOR_NONE) {
            g_api->kprintf(ATTR_YELLOW, "lspci: %u:%u.%u not present\n",
                           bus, dev, fn);
            return SH_STATUS_ERROR;
        }
        lspci_verbose_one(bus, dev, fn);
        return 0;
    }

    for (i = 0; i < n; i++) {
        if (g_api->pci_get((u32)i, &d) != 0) continue;
        if (i > 0) g_api->kprintf(ATTR_WHITE, "%s", "\n");
        lspci_verbose_one((u32)d.bus, (u32)d.dev, (u32)d.fn);
    }
    return 0;

usage:
    g_api->kprintf(ATTR_RED, "lspci: -v [bus:dev.fn | bus dev fn] "
                   "(bus 0-%u dev 0-%u fn 0-%u)\n",
                   (u32)PCI_BUS_MASK, (u32)PCI_DEV_MASK, (u32)PCI_FN_MASK);
    return SH_STATUS_USAGE;
}

static int cmd_lspci(int argc, char **argv)
{
    int n, i, b;
    PciDev d;

    if (argc >= 2) {
        if (strcmp(argv[1], "-v") == 0) return cmd_lspci_verbose(argc, argv);
        shell_print_help(argv[0]);
        return SH_STATUS_USAGE;
    }

    n = g_api->pci_count();
    if (n <= 0) {
        /* PCI のある機械には必ずホストブリッヂ (PCMC = デバイス 0、
         * io_pci.md 55 行) が居るので、0 件 = メカニズム #1 が無い。
         * **これは失敗ではない** — NP21/W の正しい姿 ([V4])。 */
        g_api->kprintf(ATTR_CYAN, "%s",
                       "lspci: no PCI (mechanism #1 not present)\n");
        return 0;
    }

    for (i = 0; i < n; i++) {
        const char *vn;

        if (g_api->pci_get((u32)i, &d) != 0) continue;

        vn = pci_vendor_name(d.vendor);
        /* **1 行を 2 回に分けて出す。** `sh.bin` (CPL=3) の KAPI は
         * int 0x80 でユーザスタックから引数を写すが、その窓は 64 バイト
         * (exec/exec.c の RING3_ARG_WINDOW)。1 回にまとめると 15 引数
         * = 60 バイトで、余白が 1 引数しか残らない。 */
        g_api->kprintf(ATTR_WHITE, "%u:%u.%u %04x:%04x %s%s%s",
                       (u32)d.bus, (u32)d.dev, (u32)d.fn,
                       (u32)d.vendor, (u32)d.device,
                       vn, (vn[0] != '\0') ? " " : "",
                       pci_class_name(d.cls, d.subclass));
        g_api->kprintf(ATTR_WHITE, " class %02x.%02x hdr %02x irq %u pin %c",
                       (u32)d.cls, (u32)d.subclass, (u32)d.header,
                       (u32)d.irq_line, pci_pin_letter(d.irq_pin));

        for (b = 0; b < PCI_CFG_BAR_COUNT; b++) {
            int kind = pci_bar_kind(d.bar[b]);
            if (kind == PCI_BAR_NONE) continue;
            g_api->kprintf(ATTR_WHITE, "  bar%d %s 0x%x", b,
                           (kind == PCI_BAR_IO) ? "io" : "mem",
                           pci_bar_base(d.bar[b]));
        }
        /* 結線の注記 (KAPI v60)。**同じ idx** で引ける (列挙順が同じ)。 */
        pci_print_bind(i);
        g_api->kprintf(ATTR_WHITE, "%s", "\n");
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  pcidump — config 256 バイトの生ダンプ                              */
/*                                                                    */
/*  実機の 1 回で票 §5-2 の R2〜R4 を持ち帰るための道具。復号しない    */
/*  生のバイト列を出すので、後から手元で何度でも読み直せる。           */
/* ------------------------------------------------------------------ */
static int cmd_pcidump(int argc, char **argv)
{
    int bus, dev, fn, off;
    u32 idw;

    if (argc < 4) {
        shell_print_help(argv[0]);
        return SH_STATUS_USAGE;
    }
    bus = pci_parse_num(argv[1]);
    dev = pci_parse_num(argv[2]);
    fn  = pci_parse_num(argv[3]);
    /* 欄の幅そのもので断る ([C4]: 上限はマスクから出す)。 */
    if (bus < 0 || bus > (int)PCI_BUS_MASK ||
        dev < 0 || dev > (int)PCI_DEV_MASK ||
        fn  < 0 || fn  > (int)PCI_FN_MASK) {
        g_api->kprintf(ATTR_RED, "pcidump: bus 0-%u dev 0-%u fn 0-%u\n",
                       (u32)PCI_BUS_MASK, (u32)PCI_DEV_MASK, (u32)PCI_FN_MASK);
        return SH_STATUS_USAGE;
    }

    if (g_api->pci_count() <= 0) {
        g_api->kprintf(ATTR_CYAN, "%s",
                       "pcidump: no PCI (mechanism #1 not present)\n");
        return SH_STATUS_ERROR;
    }

    idw = g_api->pci_cfg_read32((u32)bus, (u32)dev, (u32)fn, PCI_CFG_VENDOR_ID);
    if (pci_extract16(idw, PCI_CFG_VENDOR_ID) == PCI_VENDOR_NONE) {
        g_api->kprintf(ATTR_YELLOW, "pcidump: %u:%u.%u not present\n",
                       (u32)bus, (u32)dev, (u32)fn);
        return SH_STATUS_ERROR;
    }

    g_api->kprintf(ATTR_CYAN, "pcidump %u:%u.%u\n", (u32)bus, (u32)dev, (u32)fn);
    for (off = 0; off < PCI_CFG_SPACE_SIZE; off += 16) {
        int k;
        g_api->kprintf(ATTR_WHITE, "%02x:", (u32)off);
        for (k = 0; k < 16; k += 4) {
            u32 w = g_api->pci_cfg_read32((u32)bus, (u32)dev, (u32)fn,
                                          (u32)(off + k));
            /* DWORD をリトルエンディアンのバイト列として並べる
             * (0CFCh から読んだ順と同じ = ホストで読み直せる形)。 */
            g_api->kprintf(ATTR_WHITE, " %02x %02x %02x %02x",
                           (u32)((w >> 0) & 0xFF), (u32)((w >> 8) & 0xFF),
                           (u32)((w >> 16) & 0xFF), (u32)((w >> 24) & 0xFF));
        }
        g_api->kprintf(ATTR_WHITE, "%s", "\n");
    }
    return 0;
}

/* 登録用テーブル */
static const ShellCmd pci_cmds[] = {
    { "lspci",   cmd_lspci,   "[-v [b:d.f]]", "List PCI devices (-v: all fields, raw BARs)" },
    { "pcidump", cmd_pcidump, "bus dev fn","Hex dump of a PCI config space (256 bytes)" },
    { (const char *)0, 0, 0, 0 }
};

void shell_cmd_pci_init(void)
{
    shell_register_cmds(pci_cmds);
}
