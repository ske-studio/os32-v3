/* ======================================================================== */
/*  PCI.C — PCI コンフィギュレーションメカニズム #1 と列挙                   */
/*                                                                          */
/*  契約は drivers/pci.h。ビットの読み方は drivers/pci_decode.c (ホストで   */
/*  試験済み)。ここに残るのは **I/O と走査順と記録** だけ。                 */
/*                                                                          */
/*  ■ 読むだけ ■                                                           */
/*    BAR のサイズ判定 (全 1 を書いて読み戻す) はしない。BIOS が割り当てた  */
/*    番地を一瞬でも壊すと、同じバスに居る他のデバイス (グラフィックス・    */
/*    C バスブリッヂ) を巻き込む。票 §5-2 R1 が実機で欲しいのは生値なので   */
/*    読むだけで足りる。                                                    */
/*                                                                          */
/*  ■ ハングしない ■                                                       */
/*    NP21/W には 0CF8h が無い。読み戻しが一致しなければ**その場で**諦めて  */
/*    「PCI 無し」を報告する。走査そのものも (a) デバイス 32 × ファンクション 8 */
/*    の二重ループ、(b) ブリッヂは深さ PCI_MAX_DEPTH まで、(c) 潜る先の     */
/*    secondary バス番号が現在のバスより大きいときだけ、の 3 つで必ず終わる。 */
/*                                                                          */
/*  票  : docs/tasks/realhw/TASK_LAN_82557.md §2 L-A                        */
/*  出典: docs/hw/undocumented/io_pci.md 40〜75 行 (図1/図2)、              */
/*        446〜479 行 (0CF8h / 0CFCh)、PCI 規格の Type 0/Type 1 ヘッダ      */
/* ======================================================================== */

#include "pci.h"
#include "io.h"
#include "kprintf.h"
#include "pc98.h"

/* 記録の並びが KAPI の写し (userland/shell/cmd_pci.c) と食い違わないよう、
 * 大きさをコンパイル時に固定する (drivers/ide.h の IdeInfo と同じ作法。
 * あちらは 92B/96B の食い違いで呼び手のスタックを踏んだ)。 */
STATIC_ASSERT(sizeof(struct pci_dev) == PCI_DEV_STRUCT_SIZE, pci_dev_is_40);

static struct pci_dev g_pci[PCI_MAX_DEVS];
static int g_pci_count;      /* 記録した数 */
static int g_pci_seen;       /* 出会った数 (溢れも含む) */
static int g_pci_present;    /* メカニズム #1 が居るか */

/* ======================================================================== */
/*  メカニズム #1 の有無                                                    */
/*                                                                          */
/*  0CF8h へ既知の値を書いて読み戻す。一致しなければ PCI は無い。           */
/*  **0CF8h は DWORD アクセス必須** (io_pci.md 456 行: バイト/ワードで       */
/*  叩くと通常の I/O アクセスとして扱われ、PCI ではない別のレジスタに       */
/*  当たる)。元の値は書き戻す — PnP BIOS が何か入れたまま残している         */
/*  可能性があり、読むだけの我々が壊す理由は無い。                          */
/* ======================================================================== */
static int pci_probe_mech1(void)
{
    unsigned int flags;
    u32 saved, back_a, back_b;

    flags = irq_save();
    saved = inpd(PCI_CFG_ADDR_PORT);

    outpd(PCI_CFG_ADDR_PORT, PCI_MECH1_PROBE_A);
    back_a = inpd(PCI_CFG_ADDR_PORT);
    outpd(PCI_CFG_ADDR_PORT, PCI_MECH1_PROBE_B);
    back_b = inpd(PCI_CFG_ADDR_PORT);

    outpd(PCI_CFG_ADDR_PORT, saved);
    irq_restore(flags);

    /* 2 本とも一致して初めて「本物」。A (bit31 だけ) が通るだけの機械は、
     * 0CF8h が素通しの 32 ビットラッチである可能性がある
     * (io_pci.md 458 行: bit31=0 のとき 0CF8h〜0CFAh はマーキュリー
     * チップセット互換の別レジスタ)。B は bus/dev/fn/reg の全ビットを
     * 動かすので、そこまで保持できるのはコンフィギュレーション
     * アドレスレジスタだけ。 */
    return (back_a == PCI_MECH1_PROBE_A) && (back_b == PCI_MECH1_PROBE_B);
}

/* ======================================================================== */
/*  コンフィギュレーション空間の読み書き                                    */
/* ======================================================================== */
u32 pci_cfg_read32(u32 bus, u32 dev, u32 fn, u32 reg)
{
    unsigned int flags;
    u32 val;

    /* PCI が無い機械では 0CFCh も無い。読むと機種によっては別の機器に
     * 当たるので、**触らずに**「不在」と同じ全ビット 1 を返す。 */
    if (!g_pci_present) return 0xFFFFFFFFUL;

    /* アドレスを置いてからデータを読むまでが 1 つの手続き。あいだに
     * 割り込みが入り、ハンドラが PCI を触るとアドレスが差し替わる。 */
    flags = irq_save();
    outpd(PCI_CFG_ADDR_PORT, pci_cfg_addr(bus, dev, fn, reg));
    val = inpd(PCI_CFG_DATA_PORT);
    irq_restore(flags);
    return val;
}

u16 pci_cfg_read16(u32 bus, u32 dev, u32 fn, u32 reg)
{
    return pci_extract16(pci_cfg_read32(bus, dev, fn, reg), reg);
}

u8 pci_cfg_read8(u32 bus, u32 dev, u32 fn, u32 reg)
{
    return pci_extract8(pci_cfg_read32(bus, dev, fn, reg), reg);
}

void pci_cfg_write32(u32 bus, u32 dev, u32 fn, u32 reg, u32 value)
{
    unsigned int flags;

    if (!g_pci_present) return;

    flags = irq_save();
    outpd(PCI_CFG_ADDR_PORT, pci_cfg_addr(bus, dev, fn, reg));
    outpd(PCI_CFG_DATA_PORT, value);
    irq_restore(flags);
}

/* ======================================================================== */
/*  16 ビット書き (票 TASK_HAL_WIRING §1-4)                                 */
/*                                                                          */
/*  Command (04h) を触るために要る。**32 ビットの read-modify-write で      */
/*  代用しない** — 同じ DWORD の上位は Status (06h) で、そこは W1C          */
/*  (1 を書くと消える) なので、読んだ値をそのまま書き戻すと                 */
/*  「いま立っているエラービットを全部消す」ことになる。                    */
/*                                                                          */
/*  0CF8h は DWORD 単位でレジスタを選び、**下位 2 ビットは 0CFCh 側の       */
/*  バイトレーンで選ぶ** (PCI 規格)。だからデータポートは                   */
/*  PCI_CFG_DATA_PORT + (reg & 2) に WORD で出す。                          */
/*  オフセットは偶数に限る (奇数は DWORD をまたぐ) — 負を返して何もしない。 */
/*                                                                          */
/*  アドレスを置いてからデータを出すまでは 1 つの手続き。**同じ irq_save の */
/*  中**に置く (読みと同じ理由)。                                           */
/* ======================================================================== */
int pci_cfg_write16(u32 bus, u32 dev, u32 fn, u32 reg, u16 value)
{
    unsigned int flags;

    if (reg & 1u) return OS32_ERR_INVAL;
    if (!g_pci_present) return OS32_ERR_NOSYS;

    flags = irq_save();
    outpd(PCI_CFG_ADDR_PORT, pci_cfg_addr(bus, dev, fn, reg));
    outpw(PCI_CFG_DATA_PORT + (reg & 2u), value);
    irq_restore(flags);
    return 0;
}

/* ======================================================================== */
/*  1 ファンクションの記録                                                  */
/* ======================================================================== */
static void pci_report(const struct pci_dev *d)
{
    int i;

    /* 実機ではこの行が画面に出る (シリアル越しにも読む)。属性は
     * kprintf の入口で PC-98 流へ直される (POLICY_DEBUG §4-52)。 */
    kprintf(TATTR_WHITE, "[pci] %u:%u.%u %04x:%04x class %02x.%02x irq %u pin %u",
            (u32)d->bus, (u32)d->dev, (u32)d->fn,
            (u32)d->vendor, (u32)d->device,
            (u32)d->class, (u32)d->subclass,
            (u32)d->irq_line, (u32)d->irq_pin);

    for (i = 0; i < PCI_CFG_BAR_COUNT; i++) {
        int kind = pci_bar_kind(d->bar[i]);
        if (kind == PCI_BAR_NONE) continue;
        if (kind == PCI_BAR_IO) {
            kprintf(TATTR_WHITE, " bar%d io=0x%x", i, pci_bar_base(d->bar[i]));
        } else {
            kprintf(TATTR_WHITE, " bar%d mem=0x%x", i, pci_bar_base(d->bar[i]));
        }
    }
    kprintf(TATTR_WHITE, "\n");
}

static void pci_record(u32 bus, u32 dev, u32 fn, u16 vendor, u32 id_dword)
{
    struct pci_dev *d;
    u32 w;
    int i;

    g_pci_seen++;
    if (g_pci_count >= PCI_MAX_DEVS) return;   /* 溢れても走査は続ける */

    d = &g_pci[g_pci_count];
    d->bus = (u8)bus;
    d->dev = (u8)dev;
    d->fn  = (u8)fn;
    d->vendor = vendor;
    /* Vendor と Device は同じ DWORD。1 回の読みで両方取れる。 */
    d->device = pci_extract16(id_dword, PCI_CFG_DEVICE_ID);

    w = pci_cfg_read32(bus, dev, fn, PCI_CFG_COMMAND);
    d->command = pci_extract16(w, PCI_CFG_COMMAND);

    w = pci_cfg_read32(bus, dev, fn, PCI_CFG_REVISION_ID);
    d->progif   = pci_extract8(w, PCI_CFG_PROG_IF);
    d->subclass = pci_extract8(w, PCI_CFG_SUBCLASS);
    d->class    = pci_extract8(w, PCI_CFG_CLASS);

    w = pci_cfg_read32(bus, dev, fn, PCI_CFG_CACHE_LINE);
    d->header = pci_extract8(w, PCI_CFG_HEADER_TYPE);

    /* BAR は**生値のまま**。票 §5-2 R1 が実機で見たいのは「BIOS が
     * 割り当てたか」なので、復号も正規化もせずに持つ。
     * Type 1 (ブリッヂ) の BAR は 2 本しか無いが、残りを読んでも
     * 0 か未定義が返るだけで害は無い (**書かない**のが効いている)。 */
    for (i = 0; i < PCI_CFG_BAR_COUNT; i++) {
        d->bar[i] = pci_cfg_read32(bus, dev, fn,
                                   (u32)(PCI_CFG_BAR0 + 4 * i));
    }

    w = pci_cfg_read32(bus, dev, fn, PCI_CFG_INT_LINE);
    d->irq_line = pci_extract8(w, PCI_CFG_INT_LINE);
    d->irq_pin  = pci_extract8(w, PCI_CFG_INT_PIN);

    g_pci_count++;
    pci_report(d);
}

/* ======================================================================== */
/*  走査                                                                    */
/* ======================================================================== */
static void pci_scan_bus(u32 bus, int depth)
{
    u32 dev, fn;

    for (dev = 0; dev < PCI_DEV_COUNT; dev++) {
        u32 id0;
        u32 nfn;
        u8 hdr0;

        /* fn 0 が居なければ、そのデバイス番号には何も無い (PCI 規格:
         * ファンクションは 0 から詰まる)。 */
        id0 = pci_cfg_read32(bus, dev, 0, PCI_CFG_VENDOR_ID);
        if (pci_extract16(id0, PCI_CFG_VENDOR_ID) == PCI_VENDOR_NONE) continue;

        hdr0 = pci_cfg_read8(bus, dev, 0, PCI_CFG_HEADER_TYPE);
        nfn = pci_is_multifunction(hdr0) ? PCI_FN_COUNT : 1;

        for (fn = 0; fn < nfn; fn++) {
            u32 idw;
            u16 vendor;
            u8 hdr;

            idw = (fn == 0) ? id0
                            : pci_cfg_read32(bus, dev, fn, PCI_CFG_VENDOR_ID);
            vendor = pci_extract16(idw, PCI_CFG_VENDOR_ID);
            if (vendor == PCI_VENDOR_NONE) continue;   /* 穴あきも有り得る */

            pci_record(bus, dev, fn, vendor, idw);

            /* PCI-PCI ブリッヂなら配下のバスも見る。判定は **Header Type の
             * bit7 を落としてから** (マルチファンクションのブリッヂ 0x81 を
             * Type 0 と読むと配下を丸ごと見落とす)。 */
            hdr = (fn == 0) ? hdr0
                            : pci_cfg_read8(bus, dev, fn, PCI_CFG_HEADER_TYPE);
            if (pci_header_layout(hdr) == PCI_HDR_LAYOUT_BRIDGE &&
                depth < PCI_MAX_DEPTH) {
                u32 sec = pci_cfg_read8(bus, dev, fn, PCI_CFG_SECONDARY_BUS);
                /* **番号が増える向きにしか潜らない。** BIOS が設定を
                 * 終えていない機械では secondary に 0 やゴミが入っていて、
                 * 素直に従うと同じバスを無限に走査する。深さ上限と
                 * 合わせて「必ず終わる」を二重に担保する。 */
                if (sec > bus && sec <= PCI_BUS_MASK) {
                    pci_scan_bus(sec, depth + 1);
                }
            }
        }
    }
}

/* ======================================================================== */
/*  公開                                                                    */
/* ======================================================================== */
int pci_init(void)
{
    g_pci_count = 0;
    g_pci_seen = 0;
    g_pci_present = 0;

    if (!pci_probe_mech1()) {
        /* NP21/W はここで止まる。**これが正しい報告**であって失敗ではない。 */
        kprintf(TATTR_CYAN, "[pci] mech#1 absent\n");
        return 0;
    }
    g_pci_present = 1;
    kprintf(TATTR_GREEN, "[pci] mech#1 ok\n");

    pci_scan_bus(0, 0);

    if (g_pci_seen > g_pci_count) {
        kprintf(TATTR_YELLOW, "[pci] %d devices (%d seen, table full)\n",
                g_pci_count, g_pci_seen);
    } else {
        kprintf(TATTR_WHITE, "[pci] %d devices\n", g_pci_count);
    }
    return g_pci_count;
}

int pci_present(void)
{
    return g_pci_present;
}

int pci_count(void)
{
    return g_pci_count;
}

int pci_seen(void)
{
    return g_pci_seen;
}

int pci_get(u32 idx, void *out)
{
    struct pci_dev *dst;
    const u8 *src;
    u8 *d;
    int i;

    if (out == 0) return -1;
    if (idx >= (u32)g_pci_count) return -1;

    /* 写す。**内部配列のポインタは返さない** — CPL=3 のアプリから
     * KAPI 経由で呼ばれるので、カーネル番地を渡すとそこで死ぬ
     * (POLICY_DEBUG §4-13 の db_last_error と同じ事故)。 */
    dst = (struct pci_dev *)out;
    src = (const u8 *)&g_pci[idx];
    d = (u8 *)dst;
    for (i = 0; i < PCI_DEV_STRUCT_SIZE; i++) d[i] = src[i];
    return 0;
}

int pci_find(u16 vendor, u16 device)
{
    int i;
    for (i = 0; i < g_pci_count; i++) {
        if (g_pci[i].vendor == vendor && g_pci[i].device == device) return i;
    }
    return -1;
}
