/* ======================================================================== */
/*  PCI_BIND_HOST.C — drivers/pci_bind_match.c をそのままホストで回す       */
/*                                                                          */
/*  **NP21/W には PCI が無い**ので、この層はエミュレータでは 1 行も走らない */
/*  (`pci_init()` が「mech#1 absent」で終わる)。実機の日に初めて通る経路に  */
/*  したくないので、一致規則と DECLINE / QUARANTINE の遷移をここで固定する。*/
/*                                                                          */
/*  probe は呼び手が渡す関数ポインタなので、偽 driver を並べれば遷移は      */
/*  全部踏める — I/O も PCI も要らない。                                    */
/* ======================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../drivers/pci_bind_match.c"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #x); failed++; \
} } while (0)

static int failed;

/* ------------------------------------------------------------------ */
/*  偽 driver — 何回呼ばれたかを数える                                 */
/* ------------------------------------------------------------------ */
static int called[8];
static int ret_of[8];
static u8  reason_of[8];   /* 0 = 理由を書かない driver */

static int probe_n(int n, const struct pci_dev *dev)
{
    (void)dev;
    called[n]++;
    if (reason_of[n]) pci_bind_set_reason(reason_of[n]);
    return ret_of[n];
}

static int probe0(const struct pci_dev *d) { return probe_n(0, d); }
static int probe1(const struct pci_dev *d) { return probe_n(1, d); }
static int probe2(const struct pci_dev *d) { return probe_n(2, d); }

static struct pci_driver D0 = { sizeof(struct pci_driver), 0x8086, 0x1229,
                                PCI_MATCH_ANY8, PCI_MATCH_ANY8, "d0", probe0 };
static struct pci_driver D1 = { sizeof(struct pci_driver), 0x8086, 0x1229,
                                PCI_MATCH_ANY8, PCI_MATCH_ANY8, "d1", probe1 };
static struct pci_driver D2 = { sizeof(struct pci_driver), PCI_MATCH_ANY16,
                                PCI_MATCH_ANY16, 0x02, 0x00, "d2", probe2 };

static const struct pci_driver *const TBL3[] = { &D0, &D1, &D2 };

static struct pci_dev DEV;

static void reset(void)
{
    memset(called, 0, sizeof(called));
    memset(ret_of, 0, sizeof(ret_of));
    memset(reason_of, 0, sizeof(reason_of));
    memset(&DEV, 0, sizeof(DEV));
    DEV.bus = 0; DEV.dev = 8; DEV.fn = 0;
    DEV.vendor = 0x8086; DEV.device = 0x1229;
    DEV.class = 0x02; DEV.subclass = 0x00;
    DEV.irq_line = 5;
    pci_bind_set_line_state_hook((pci_bind_line_state_fn)0);
    pci_bind_reason_reset();
}

/* ------------------------------------------------------------------ */
/*  4 欄の AND と「任意」                                              */
/* ------------------------------------------------------------------ */
static void match_rules(void)
{
    struct pci_driver d;

    reset();
    d = D0;
    CHECK(pci_bind_match(&d, &DEV) == 1);

    /* vendor が違えば外れる */
    d = D0; d.vendor = 0x1011;
    CHECK(pci_bind_match(&d, &DEV) == 0);
    /* device が違えば外れる */
    d = D0; d.device = 0x1030;
    CHECK(pci_bind_match(&d, &DEV) == 0);
    /* class / subclass も AND */
    d = D0; d.class = 0x01;
    CHECK(pci_bind_match(&d, &DEV) == 0);
    d = D0; d.class = 0x02; d.subclass = 0x80;
    CHECK(pci_bind_match(&d, &DEV) == 0);
    d = D0; d.class = 0x02; d.subclass = 0x00;
    CHECK(pci_bind_match(&d, &DEV) == 1);

    /* **任意は素通し** — 4 欄全部が任意なら何にでも当たる */
    d.vendor = PCI_MATCH_ANY16; d.device = PCI_MATCH_ANY16;
    d.class = PCI_MATCH_ANY8; d.subclass = PCI_MATCH_ANY8;
    CHECK(pci_bind_match(&d, &DEV) == 1);
    DEV.vendor = 0x1234; DEV.device = 0x5678;
    DEV.class = 0xFE; DEV.subclass = 0xEF;
    CHECK(pci_bind_match(&d, &DEV) == 1);

    /* **0xFF の class を持つ装置に 0xFF の欄が当たる**のは「任意」だから
     * であって偶然ではない。逆に 0xFE の装置に 0xFF は当たらない…のでは
     * なく当たる (任意なので)。ここを取り違えると「未知の装置に全部の
     * driver が当たる」ことに気づけない。 */
    d.class = PCI_MATCH_ANY8;
    CHECK(pci_bind_match(&d, &DEV) == 1);

    CHECK(pci_bind_match((const struct pci_driver *)0, &DEV) == 0);
    CHECK(pci_bind_match(&d, (const struct pci_dev *)0) == 0);
}

/* ------------------------------------------------------------------ */
/*  候補探索の順番                                                     */
/* ------------------------------------------------------------------ */
static void next_order(void)
{
    reset();
    /* D0 / D1 が vendor:device で、D2 が class で当たる = 3 本とも候補 */
    CHECK(pci_bind_next(TBL3, 3, 0, &DEV) == 0);
    CHECK(pci_bind_next(TBL3, 3, 1, &DEV) == 1);
    CHECK(pci_bind_next(TBL3, 3, 2, &DEV) == 2);
    CHECK(pci_bind_next(TBL3, 3, 3, &DEV) == -1);
    /* 表が空 / NULL */
    CHECK(pci_bind_next(TBL3, 0, 0, &DEV) == -1);
    CHECK(pci_bind_next((const struct pci_driver *const *)0, 3, 0, &DEV) == -1);

    /* class の違う装置には D2 だけが外れる */
    DEV.class = 0x0C;
    CHECK(pci_bind_next(TBL3, 3, 0, &DEV) == 0);
    CHECK(pci_bind_next(TBL3, 3, 2, &DEV) == -1);
}

/* ------------------------------------------------------------------ */
/*  DECLINE → 次へ / OK で止まる                                       */
/* ------------------------------------------------------------------ */
static void decline_chain(void)
{
    struct pci_bind_info info;

    reset();
    ret_of[0] = PCI_PROBE_DECLINE;
    ret_of[1] = PCI_PROBE_OK;
    CHECK(pci_bind_one(TBL3, 3, &DEV, &info) == PCI_PROBE_OK);
    CHECK(called[0] == 1);
    CHECK(called[1] == 1);
    CHECK(called[2] == 0);            /* OK で止まる */
    CHECK(info.result == PCI_BIND_BOUND);
    CHECK(info.reason == PCI_BIND_OK);   /* DECLINE の理由で上書きされない */
    CHECK(info.bus == 0 && info.dev == 8 && info.fn == 0);
    CHECK(info.irq == 5);

    /* 全部 DECLINE → **最後の** DECLINED が残る */
    reset();
    ret_of[0] = PCI_PROBE_DECLINE;
    ret_of[1] = PCI_PROBE_DECLINE;
    ret_of[2] = PCI_PROBE_DECLINE;
    reason_of[0] = PCI_BIND_IRQ_UNSUPPORTED;
    reason_of[2] = PCI_BIND_START_FAILED;
    CHECK(pci_bind_one(TBL3, 3, &DEV, &info) == PCI_PROBE_DECLINE);
    CHECK(called[0] == 1 && called[1] == 1 && called[2] == 1);
    CHECK(info.result == PCI_BIND_DECLINED);
    CHECK(info.reason == PCI_BIND_START_FAILED);

    /* 一致が 1 つも無ければ NONE / NO_DRIVER (probe は呼ばれない) */
    reset();
    DEV.vendor = 0x1011; DEV.device = 0x0019; DEV.class = 0x01;
    CHECK(pci_bind_one(TBL3, 3, &DEV, &info) == PCI_PROBE_DECLINE);
    CHECK(called[0] == 0 && called[1] == 0 && called[2] == 0);
    CHECK(info.result == PCI_BIND_NONE);
    CHECK(info.reason == PCI_BIND_NO_DRIVER);
}

/* ------------------------------------------------------------------ */
/*  QUARANTINE で打ち切る                                              */
/* ------------------------------------------------------------------ */
static void quarantine_stops(void)
{
    struct pci_bind_info info;

    reset();
    ret_of[0] = PCI_PROBE_QUARANTINE;
    ret_of[1] = PCI_PROBE_OK;         /* **呼ばれてはいけない** */
    CHECK(pci_bind_one(TBL3, 3, &DEV, &info) == PCI_PROBE_QUARANTINE);
    CHECK(called[0] == 1);
    CHECK(called[1] == 0);
    CHECK(called[2] == 0);
    CHECK(info.result == PCI_BIND_QUARANTINED);
    /* 理由を書かなかった QUARANTINE は「reset が Idle を作れなかった」 */
    CHECK(info.reason == PCI_BIND_RESET_FAILED);

    /* 理由を書けばそれが残る */
    reset();
    ret_of[0] = PCI_PROBE_DECLINE;
    ret_of[1] = PCI_PROBE_QUARANTINE;
    reason_of[1] = PCI_BIND_NOISY;
    CHECK(pci_bind_one(TBL3, 3, &DEV, &info) == PCI_PROBE_QUARANTINE);
    CHECK(called[2] == 0);
    CHECK(info.result == PCI_BIND_QUARANTINED);
    CHECK(info.reason == PCI_BIND_NOISY);
}

/* ------------------------------------------------------------------ */
/*  理由は候補ごとに初期化する (往復 9 R2 / 往復 10 R2)                */
/* ------------------------------------------------------------------ */
static void reason_reset(void)
{
    struct pci_bind_info info;

    reset();
    /* 1 本目が理由を書き、2 本目が書かずに DECLINE。
     * **1 本目の理由が残ってはいけない**。 */
    ret_of[0] = PCI_PROBE_DECLINE;
    ret_of[1] = PCI_PROBE_DECLINE;
    ret_of[2] = PCI_PROBE_DECLINE;
    reason_of[0] = PCI_BIND_IRQ_QUARANTINED;
    reason_of[1] = 0;
    reason_of[2] = 0;
    CHECK(pci_bind_one(TBL3, 3, &DEV, &info) == PCI_PROBE_DECLINE);
    CHECK(info.result == PCI_BIND_DECLINED);
    CHECK(info.reason == PCI_BIND_DECLINED_UNSPECIFIED);
    CHECK(info.reason != PCI_BIND_IRQ_QUARANTINED);
    /* 「理由不明」は「問題なし」ではない。 */
    CHECK(info.reason != PCI_BIND_OK);

    /* 逆順 (書かない → 書く) では書いたほうが残る */
    reset();
    ret_of[0] = PCI_PROBE_DECLINE;
    ret_of[1] = PCI_PROBE_DECLINE;
    ret_of[2] = PCI_PROBE_DECLINE;
    reason_of[2] = PCI_BIND_IRQ_UNSUPPORTED;
    CHECK(pci_bind_one(TBL3, 3, &DEV, &info) == PCI_PROBE_DECLINE);
    CHECK(info.reason == PCI_BIND_IRQ_UNSUPPORTED);
}

/* ------------------------------------------------------------------ */
/*  線の様子は読む時点で合成する (往復 9 R1 / 往復 10 R1)              */
/* ------------------------------------------------------------------ */
static u8 hook_bits;
static unsigned int hook_irq_seen;

static u8 line_hook(unsigned int irq)
{
    hook_irq_seen = irq;
    return hook_bits;
}

static void line_state(void)
{
    struct pci_bind_info info;

    reset();
    ret_of[0] = PCI_PROBE_OK;
    CHECK(pci_bind_one(TBL3, 3, &DEV, &info) == PCI_PROBE_OK);
    CHECK(info.result == PCI_BIND_BOUND);

    /* hook が無ければ OK */
    pci_bind_info_compose(&info);
    CHECK(info.line_state == PCI_LINE_OK);
    CHECK(info.pad == 0);

    /* **結線した後で線が隔離される**: result は BOUND のまま、
     * line_state だけが変わる (保存値ではなく読む時点で合成するから)。 */
    pci_bind_set_line_state_hook(line_hook);
    hook_bits = PCI_LINE_BIT_QUARANTINED;
    pci_bind_info_compose(&info);
    CHECK(info.result == PCI_BIND_BOUND);
    CHECK(info.line_state == PCI_LINE_QUARANTINED);
    CHECK(hook_irq_seen == 5);

    /* ストームのマスクだけ */
    hook_bits = PCI_LINE_BIT_STORM;
    pci_bind_info_compose(&info);
    CHECK(info.line_state == PCI_LINE_STORM_MASKED);

    /* **両方立っていれば隔離が勝つ** — ストームのマスクは再計算で外れ
     * 得るが、隔離は再起動まで戻らない。弱いほうを名乗ると復旧済みに
     * 見える。 */
    hook_bits = (u8)(PCI_LINE_BIT_STORM | PCI_LINE_BIT_QUARANTINED);
    pci_bind_info_compose(&info);
    CHECK(info.line_state == PCI_LINE_QUARANTINED);

    /* 線が戻れば OK に戻る (保存していない証拠) */
    hook_bits = 0;
    pci_bind_info_compose(&info);
    CHECK(info.line_state == PCI_LINE_OK);

    /* **未割り当て (0xFF) と 16 以上は hook を呼ばずに OK**。
     * 0xFF をそのままシフトすると未定義。 */
    hook_bits = PCI_LINE_BIT_QUARANTINED;
    hook_irq_seen = 0xDEAD;
    info.irq = PCI_IRQ_UNASSIGNED;
    pci_bind_info_compose(&info);
    CHECK(info.line_state == PCI_LINE_OK);
    CHECK(hook_irq_seen == 0xDEAD);       /* 呼んでいない */
    info.irq = PCI_BIND_IRQ_MAX;
    pci_bind_info_compose(&info);
    CHECK(info.line_state == PCI_LINE_OK);
    CHECK(hook_irq_seen == 0xDEAD);
    info.irq = PCI_BIND_IRQ_MAX - 1;
    pci_bind_info_compose(&info);
    CHECK(info.line_state == PCI_LINE_QUARANTINED);
    CHECK(hook_irq_seen == PCI_BIND_IRQ_MAX - 1);

    pci_bind_info_compose((struct pci_bind_info *)0);   /* 落ちない */
    pci_bind_set_line_state_hook((pci_bind_line_state_fn)0);
}

/* ------------------------------------------------------------------ */
/*  記録の取得口 (KAPI v60 `pci_bind_info` の裏側)                      */
/*                                                                    */
/*  `drivers/pci_bind.c` は列挙表 (`pci_count` / `pci_get`) と kprintf */
/*  しか外に頼らないので、その 3 本を偽物にすればホストでそのまま回る。*/
/*  ここで固めるのは 2 つ: **8 バイトちょうど**しか書かないことと、    */
/*  範囲外の idx で**出力に 1 バイトも書かない**こと。KAPI v60 の      */
/*  wrapper (`kapi/kapi_sys.c`) は 8 バイトぶんだけ書き込み可を確かめて */
/*  から呼ぶので、ここが増えると CPL=3 のバッファをはみ出す。          */
/* ------------------------------------------------------------------ */
#include "kprintf.h"

static struct pci_dev fake_devs[4];
static int fake_count;

int pci_count(void) { return fake_count; }

int pci_get(u32 idx, void *out)
{
    if (!out || idx >= (u32)fake_count) return -1;
    memcpy(out, &fake_devs[idx], sizeof(struct pci_dev));
    return 0;
}

void kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }

#include "../../drivers/pci_bind.c"

/* 8 バイトの記録 + その後ろの見張り。union なので型の別名の心配が無い。 */
static union { struct pci_bind_info info; u8 raw[16]; } out_buf;

static void fill_guard(u8 v)
{
    memset(out_buf.raw, v, sizeof(out_buf.raw));
}

static int guard_intact(u8 v)
{
    int i;
    for (i = PCI_BIND_INFO_SIZE; i < (int)sizeof(out_buf.raw); i++)
        if (out_buf.raw[i] != v) return 0;
    return 1;
}

static int all_untouched(u8 v)
{
    int i;
    for (i = 0; i < (int)sizeof(out_buf.raw); i++)
        if (out_buf.raw[i] != v) return 0;
    return 1;
}

static void info_get(void)
{
    reset();

    /* 2 台。1 台目は D0 が受け、2 台目は一致する driver が無い。 */
    memset(fake_devs, 0, sizeof(fake_devs));
    fake_devs[0] = DEV;                    /* 8086:1229 class 02.00 irq 5 */
    fake_devs[1] = DEV;
    fake_devs[1].dev = 9;
    fake_devs[1].vendor = 0x1011;
    fake_devs[1].device = 0x0019;
    fake_devs[1].class = 0x01;
    fake_devs[1].subclass = 0x00;
    fake_devs[1].irq_line = 11;
    fake_count = 2;

    ret_of[0] = PCI_PROBE_OK;
    CHECK(pci_bind_all(TBL3, 3) == 1);
    CHECK(pci_quarantined() == 0);
    CHECK(called[0] == 1);

    /* **8 バイトちょうど**。9 バイト目から先は触らない。 */
    fill_guard(0xAA);
    CHECK(pci_bind_info_get(0, &out_buf.info) == 0);
    CHECK(guard_intact(0xAA));
    CHECK(out_buf.info.bus == 0);
    CHECK(out_buf.info.dev == 8);
    CHECK(out_buf.info.fn == 0);
    CHECK(out_buf.info.result == PCI_BIND_BOUND);
    CHECK(out_buf.info.reason == PCI_BIND_OK);
    CHECK(out_buf.info.irq == 5);
    CHECK(out_buf.info.line_state == PCI_LINE_OK);
    CHECK(out_buf.info.pad == 0);

    /* 2 台目は「一致する driver が無い」— これは失敗ではない。 */
    fill_guard(0xAA);
    CHECK(pci_bind_info_get(1, &out_buf.info) == 0);
    CHECK(guard_intact(0xAA));
    CHECK(out_buf.info.dev == 9);
    CHECK(out_buf.info.result == PCI_BIND_NONE);
    CHECK(out_buf.info.reason == PCI_BIND_NO_DRIVER);
    CHECK(out_buf.info.irq == 11);

    /* **範囲外は断り、出力に 1 バイトも書かない。** */
    fill_guard(0x55);
    CHECK(pci_bind_info_get(2, &out_buf.info) == -1);
    CHECK(all_untouched(0x55));
    CHECK(pci_bind_info_get(3, &out_buf.info) == -1);
    CHECK(all_untouched(0x55));
    CHECK(pci_bind_info_get(-1, &out_buf.info) == -1);
    CHECK(all_untouched(0x55));
    CHECK(pci_bind_info_get(PCI_MAX_DEVS, &out_buf.info) == -1);
    CHECK(all_untouched(0x55));
    /* NULL も断る (KAPI の wrapper より手前で落ちないこと) */
    CHECK(pci_bind_info_get(0, (struct pci_bind_info *)0) == -1);

    /* **線の様子は読む時点で合成する** — 結線のあとで隔離されても
     * result は BOUND のまま line_state だけが変わる。 */
    pci_bind_set_line_state_hook(line_hook);
    hook_bits = PCI_LINE_BIT_QUARANTINED;
    fill_guard(0xAA);
    CHECK(pci_bind_info_get(0, &out_buf.info) == 0);
    CHECK(guard_intact(0xAA));
    CHECK(out_buf.info.result == PCI_BIND_BOUND);
    CHECK(out_buf.info.line_state == PCI_LINE_QUARANTINED);
    hook_bits = 0;
    CHECK(pci_bind_info_get(0, &out_buf.info) == 0);
    CHECK(out_buf.info.line_state == PCI_LINE_OK);
    pci_bind_set_line_state_hook((pci_bind_line_state_fn)0);

    /* 列挙が 0 件なら記録も 0 件 (NP21/W の姿)。 */
    fake_count = 0;
    CHECK(pci_bind_all(TBL3, 3) == 0);
    CHECK(pci_bind_info_get(0, &out_buf.info) == -1);
}

/* ------------------------------------------------------------------ */
/*  複数の装置 — 同じ driver が 2 台に当たってよい                     */
/* ------------------------------------------------------------------ */
static void multi_dev(void)
{
    struct pci_bind_info i1, i2;
    struct pci_dev d2;

    reset();
    ret_of[0] = PCI_PROBE_OK;
    CHECK(pci_bind_one(TBL3, 3, &DEV, &i1) == PCI_PROBE_OK);

    d2 = DEV;
    d2.dev = 9;
    d2.irq_line = 9;
    /* 2 台目は同じ driver が DECLINE し、次の候補が受ける */
    ret_of[0] = PCI_PROBE_DECLINE;
    ret_of[1] = PCI_PROBE_OK;
    CHECK(pci_bind_one(TBL3, 3, &d2, &i2) == PCI_PROBE_OK);
    CHECK(called[0] == 2);            /* 2 台とも同じ driver に当たった */
    CHECK(i1.dev == 8 && i2.dev == 9);
    CHECK(i1.irq == 5 && i2.irq == 9);
    CHECK(i1.result == PCI_BIND_BOUND && i2.result == PCI_BIND_BOUND);

    /* 記録は 8 バイトちょうど (KAPI v60 の出力保護がこの大きさを通す) */
    CHECK(sizeof(struct pci_bind_info) == PCI_BIND_INFO_SIZE);
    CHECK(PCI_BIND_INFO_SIZE == 8);
}

int main(int argc, char **argv)
{
    const char *c = (argc > 1) ? argv[1] : "";

    if (!strcmp(c, "match_rules"))            match_rules();
    else if (!strcmp(c, "next_order"))        next_order();
    else if (!strcmp(c, "decline_chain"))     decline_chain();
    else if (!strcmp(c, "quarantine_stops"))  quarantine_stops();
    else if (!strcmp(c, "reason_reset"))      reason_reset();
    else if (!strcmp(c, "line_state"))        line_state();
    else if (!strcmp(c, "multi_dev"))         multi_dev();
    else if (!strcmp(c, "info_get"))          info_get();
    else { fprintf(stderr, "unknown case: %s\n", c); return 2; }
    return failed ? 1 : 0;
}
