/* ======================================================================== */
/*  LSPCI_CMD_HOST.C — `lspci` / `lspci -v` のコマンド層をホストで回す       */
/*                                                                          */
/*  実物の userland/shell/cmd_pci.c を #include し、KernelAPI だけを偽物に   */
/*  差し替える。見るのは pci_verbose.c (行づくり) の外側:                    */
/*    - 引数の検査が PCI の有無より先に来ること (Codex P3)                  */
/*    - config を**どこを何回**読むか (0x00〜0x3C を 1 回ずつ、書かない)     */
/*    - `lspci -v B:D.F` と `lspci -v bus dev fn` が同じ出力になること      */
/*    - 引数なしの `lspci` の出力が変わっていないこと (回帰)                */
/*                                                                          */
/*  NP21/W には PCI が無いので、実機以外で走らせられるのはここだけ。         */
/*  試験: tools/tests/test_pci_decode.py (lspci_* のケース)                 */
/* ======================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

/* ---- 贋の shell.h ------------------------------------------------------
 * 本物の shell.h は os32_kapi_shared.h 経由で u32 を unsigned long にする
 * (ILP32 のターゲットでは 32 ビット)。ホスト (LP64) ではそれが 64 ビットに
 * なり、PciDev の STATIC_ASSERT (40 バイト) と kprintf の %u が合わない。
 * そこで net_link_host.c と同じく 32 ビット幅の型を先に置き、cmd_pci.c が
 * 使う分だけの shell.h を用意する (ガード SHELL_H を先に立てる)。
 * KernelAPI も cmd_pci.c が触る 5 本だけ — 同じ翻訳単位で閉じるので
 * 本物の並びと合わせる必要はない。 */
#define __cdecl
#define OS32_KAPI_SHARED_H
typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef signed char    i8;
typedef signed short   i16;
typedef signed int     i32;
#include "types.h"          /* STATIC_ASSERT (型は上の 32 ビット幅) */

#define ATTR_WHITE   0xE1
#define ATTR_CYAN    0xA1
#define ATTR_GREEN   0x81
#define ATTR_YELLOW  0xC1
#define ATTR_RED     0x41

typedef struct {
    void (__cdecl *kprintf)(u8 attr, const char *fmt, ...);
    int  (__cdecl *pci_count)(void);
    int  (__cdecl *pci_get)(u32 idx, void *out);
    int  (__cdecl *pci_bind_info)(u32 idx, void *out);
    u32  (__cdecl *pci_cfg_read32)(u32 bus, u32 dev, u32 fn, u32 reg);
} KernelAPI;

#define SH_STATUS_OK        0
#define SH_STATUS_ERROR     1
#define SH_STATUS_USAGE     2

typedef int (*CmdHandler)(int argc, char **argv);
typedef struct {
    const char *name;
    CmdHandler handler;
    const char *usage;
    const char *description;
} ShellCmd;

extern KernelAPI *g_api;
void shell_register_cmds(const ShellCmd *cmds);
void shell_print_help(const char *cmd_name);
void shell_cmd_pci_init(void);
#define SHELL_H             /* cmd_pci.c の #include "shell.h" を空にする */

/* ---- 出力の写し -------------------------------------------------------- */
static char g_out[16384];
static int  g_help_calls;

static void __cdecl h_kprintf(u8 attr, const char *fmt, ...)
{
    va_list ap;
    size_t len = strlen(g_out);
    (void)attr;
    va_start(ap, fmt);
    vsnprintf(g_out + len, sizeof(g_out) - len, fmt, ap);
    va_end(ap);
}

void shell_print_help(const char *cmd_name) { (void)cmd_name; g_help_calls++; }
void shell_register_cmds(const ShellCmd *cmds) { (void)cmds; }

/* ---- 実物 (PciDev の定義もここから) --------------------------------------------------------------- */
#include "../../userland/shell/cmd_pci.c"
#include "../../drivers/pci_decode.c"
#include "../../userland/shell/pci_verbose.c"

/* ---- 偽の PCI ---------------------------------------------------------- */
typedef struct {
    u8  bus, dev, fn;
    u32 cfg[64];
} FakeFn;

static FakeFn g_fns[4];
static int    g_nfns;
static int    g_pci_present;

/* pci_cfg_read32 の呼ばれ方の記録 */
typedef struct { u32 bus, dev, fn, reg; } ReadLog;
static ReadLog g_reads[256];
static int     g_nreads;

static int __cdecl h_pci_count(void) { return g_pci_present ? g_nfns : 0; }

static int __cdecl h_pci_get(u32 idx, void *out)
{
    /* cmd_pci.c の PciDev と同じ並び (40 バイト、STATIC_ASSERT 済み)。 */
    PciDev *d = (PciDev *)out;
    const FakeFn *f;
    int b;
    if ((int)idx >= h_pci_count()) return -1;
    f = &g_fns[idx];
    memset(d, 0, sizeof(*d));
    d->bus = f->bus; d->dev = f->dev; d->fn = f->fn;
    d->vendor   = (u16)(f->cfg[0] & 0xFFFF);
    d->device   = (u16)(f->cfg[0] >> 16);
    d->command  = (u16)(f->cfg[1] & 0xFFFF);
    d->progif   = (u8)(f->cfg[2] >> 8);
    d->subclass = (u8)(f->cfg[2] >> 16);
    d->cls      = (u8)(f->cfg[2] >> 24);
    d->header   = (u8)(f->cfg[3] >> 16);
    for (b = 0; b < PCI_CFG_BAR_COUNT; b++) d->bar[b] = f->cfg[4 + b];
    d->irq_line = (u8)(f->cfg[15] & 0xFF);
    d->irq_pin  = (u8)(f->cfg[15] >> 8);
    return 0;
}

static int __cdecl h_pci_bind_info(u32 idx, void *out)
{
    (void)idx; (void)out;
    return -1;       /* 結線の注記は出さない (本題でない) */
}

static u32 __cdecl h_pci_cfg_read32(u32 bus, u32 dev, u32 fn, u32 reg)
{
    int i;
    if (g_nreads < (int)(sizeof(g_reads) / sizeof(g_reads[0]))) {
        g_reads[g_nreads].bus = bus; g_reads[g_nreads].dev = dev;
        g_reads[g_nreads].fn = fn;   g_reads[g_nreads].reg = reg;
    }
    g_nreads++;
    for (i = 0; i < g_nfns; i++) {
        if (g_fns[i].bus == bus && g_fns[i].dev == dev && g_fns[i].fn == fn)
            return g_fns[i].cfg[(reg & 0xFC) / 4];
    }
    return 0xFFFFFFFFUL;    /* 不在 */
}

static KernelAPI g_fake;
KernelAPI *g_api;


#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #x); failed++; \
} } while (0)

static int failed;

static void reset(int present)
{
    memset(&g_fake, 0, sizeof(g_fake));
    g_fake.kprintf = h_kprintf;
    g_fake.pci_count = h_pci_count;
    g_fake.pci_get = h_pci_get;
    g_fake.pci_bind_info = h_pci_bind_info;
    g_fake.pci_cfg_read32 = h_pci_cfg_read32;
    g_api = &g_fake;

    memset(g_fns, 0, sizeof(g_fns));
    /* 0:1.0 NEC の C バスブリッヂ風 (ISA ブリッヂ) */
    g_fns[0].bus = 0; g_fns[0].dev = 1; g_fns[0].fn = 0;
    g_fns[0].cfg[0]  = 0x00011033UL;
    g_fns[0].cfg[1]  = 0x02000007UL;
    g_fns[0].cfg[2]  = 0x06010001UL;
    g_fns[0].cfg[3]  = 0x00000000UL;
    /* 0:9.0 TV チューナー想定 (14F1:8800、subsystem は仮) */
    g_fns[1].bus = 0; g_fns[1].dev = 9; g_fns[1].fn = 0;
    g_fns[1].cfg[0]  = 0x880014F1UL;
    g_fns[1].cfg[1]  = 0x02900006UL;
    g_fns[1].cfg[2]  = 0x04000005UL;
    g_fns[1].cfg[3]  = 0x00802008UL;
    g_fns[1].cfg[4]  = 0xF8000000UL;
    g_fns[1].cfg[11] = 0xD00310FCUL;
    g_fns[1].cfg[15] = 0x2804010BUL;
    /* 0x40 以降は -v が読んではいけない印 */
    g_fns[1].cfg[16] = 0xDEADBEEFUL;
    g_nfns = 2;
    g_pci_present = present;

    g_out[0] = '\0';
    g_help_calls = 0;
    g_nreads = 0;
}

static int run(int argc, const char *a1, const char *a2, const char *a3,
               const char *a4)
{
    char *argv[6];
    argv[0] = (char *)"lspci";
    argv[1] = (char *)a1; argv[2] = (char *)a2;
    argv[3] = (char *)a3; argv[4] = (char *)a4; argv[5] = 0;
    return cmd_lspci(argc, argv);
}

static void expect_text(const char *got, const char *want, int line)
{
    if (strcmp(got, want) != 0) {
        fprintf(stderr, "FAIL line %d\n--- want\n%s--- got\n%s---\n",
                line, want, got);
        failed++;
    }
}

static const char TUNER_V[] =
    "0:9.0 14f1:8800 Conexant\n"
    "  revision 05\n"
    "  class 04.00.00 Multimedia/Video\n"
    "  header 80 type 0 (device) multi-function\n"
    "  command 0006 I/O- Mem+ BusMaster+\n"
    "  status 0290\n"
    "  subsystem 10fc:d003\n"
    "  bar0 f8000000 mem32 base 0xf8000000\n"
    "  bar1 00000000 zero (unimplemented or unassigned)\n"
    "  bar2 00000000 zero (unimplemented or unassigned)\n"
    "  bar3 00000000 zero (unimplemented or unassigned)\n"
    "  bar4 00000000 zero (unimplemented or unassigned)\n"
    "  bar5 00000000 zero (unimplemented or unassigned)\n"
    "  interrupt line 11 pin A\n";

/* (1) 引数なしの lspci — 変更前と同じ出力、config は直接読まない。 */
static void lspci_noarg(void)
{
    reset(1);
    CHECK(run(1, 0, 0, 0, 0) == 0);
    expect_text(g_out,
        "0:1.0 1033:0001 NEC Bridge/ISA class 06.01 hdr 00 irq 0 pin -\n"
        "0:9.0 14f1:8800 Conexant Multimedia/Video class 04.00 hdr 80 irq 11 pin A"
        "  bar0 mem 0xf8000000\n", __LINE__);
    CHECK(g_nreads == 0);

    reset(0);
    CHECK(run(1, 0, 0, 0, 0) == 0);
    expect_text(g_out, "lspci: no PCI (mechanism #1 not present)\n", __LINE__);
}

/* (2) lspci -v (全部) — 読む先と回数。 */
static void lspci_v_all(void)
{
    int i, k;
    reset(1);
    CHECK(run(2, "-v", 0, 0, 0) == 0);
    CHECK(strstr(g_out, TUNER_V) != 0);
    CHECK(strstr(g_out, "0:1.0 1033:0001 NEC\n") == g_out);
    CHECK(strstr(g_out, "interrupt line 0 pin - (none)\n\n0:9.0 ") != 0);
    /* 1 台あたり 0x00〜0x3C を**順に 1 回ずつ**、合わせて 16 回。 */
    CHECK(g_nreads == 2 * PCI_VERBOSE_CFG_DWORDS);
    for (i = 0; i < 2; i++) {
        for (k = 0; k < PCI_VERBOSE_CFG_DWORDS; k++) {
            const ReadLog *r = &g_reads[i * PCI_VERBOSE_CFG_DWORDS + k];
            CHECK(r->bus == g_fns[i].bus && r->dev == g_fns[i].dev &&
                  r->fn == g_fns[i].fn);
            CHECK(r->reg == (u32)(k * 4));
        }
    }
    CHECK(strstr(g_out, "deadbeef") == 0);

    reset(0);
    CHECK(run(2, "-v", 0, 0, 0) == 0);
    expect_text(g_out, "lspci: no PCI (mechanism #1 not present)\n", __LINE__);
    CHECK(g_nreads == 0);
}

/* (3) 1 台だけ: B:D.F と bus dev fn (3 引数) が同じ出力。 */
static void lspci_v_one(void)
{
    char first[4096];
    int k;

    reset(1);
    CHECK(run(3, "-v", "0:9.0", 0, 0) == 0);
    expect_text(g_out, TUNER_V, __LINE__);
    /* 在否の 1 回 (0x00) + 本読み 16 回。どれも 0:9.0 だけ。 */
    CHECK(g_nreads == 1 + PCI_VERBOSE_CFG_DWORDS);
    CHECK(g_reads[0].reg == PCI_CFG_VENDOR_ID);
    for (k = 0; k < g_nreads && k < 64; k++)
        CHECK(g_reads[k].bus == 0 && g_reads[k].dev == 9 && g_reads[k].fn == 0);
    strcpy(first, g_out);

    reset(1);
    CHECK(run(5, "-v", "0", "9", "0") == 0);
    expect_text(g_out, first, __LINE__);
    CHECK(g_nreads == 1 + PCI_VERBOSE_CFG_DWORDS);

    reset(1);
    CHECK(run(5, "-v", "0x0", "0x9", "0") == 0);
    expect_text(g_out, first, __LINE__);

    /* 空のスロットは失敗、読みは在否の 1 回だけ。 */
    reset(1);
    CHECK(run(3, "-v", "0:5.0", 0, 0) == SH_STATUS_ERROR);
    expect_text(g_out, "lspci: 0:5.0 not present\n", __LINE__);
    CHECK(g_nreads == 1);

    /* PCI が無い機械で 1 台を名指し = 失敗 (一覧の 0 台とは違う)。 */
    reset(0);
    CHECK(run(3, "-v", "0:9.0", 0, 0) == SH_STATUS_ERROR);
    CHECK(strstr(g_out, "no PCI") != 0);
    CHECK(g_nreads == 0);
}

/* (4) 引数の誤りは PCI の有無に関係なく USAGE、config は読まない。 */
static void lspci_badargs(void)
{
    int present;
    for (present = 0; present <= 1; present++) {
        reset(present);
        CHECK(run(3, "-v", "256:0.0", 0, 0) == SH_STATUS_USAGE);
        CHECK(strstr(g_out, "no PCI") == 0);
        CHECK(g_nreads == 0);

        reset(present);
        CHECK(run(3, "-v", "garbage", 0, 0) == SH_STATUS_USAGE);
        CHECK(g_nreads == 0);

        reset(present);
        CHECK(run(4, "-v", "0", "9", 0) == SH_STATUS_USAGE);
        CHECK(g_nreads == 0);

        reset(present);
        CHECK(run(5, "-v", "0", "32", "0") == SH_STATUS_USAGE);
        CHECK(g_nreads == 0);

        reset(present);
        CHECK(run(5, "-v", "0", "9", "8") == SH_STATUS_USAGE);
        CHECK(g_nreads == 0);

        reset(present);
        CHECK(run(2, "garbage", 0, 0, 0) == SH_STATUS_USAGE);
        CHECK(g_help_calls == 1);
        CHECK(g_nreads == 0);
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    failed = 0;
    if (!strcmp(argv[1], "lspci_noarg")) lspci_noarg();
    else if (!strcmp(argv[1], "lspci_v_all")) lspci_v_all();
    else if (!strcmp(argv[1], "lspci_v_one")) lspci_v_one();
    else if (!strcmp(argv[1], "lspci_badargs")) lspci_badargs();
    else return 2;
    if (failed) return 1;
    printf("PASS %s\n", argv[1]);
    return 0;
}
