/* PCI enumeration, configuration access guards and device records. */
#include "pci.h"
#include "pci_config_contract.h"
#include "kprintf.h"

/* 記録の並びが KAPI の写し (userland/shell/cmd_pci.c) と食い違わないよう、
 * 大きさをコンパイル時に固定する (sys/dev/ata/ide.h の IdeInfo と同じ作法。
 * あちらは 92B/96B の食い違いで呼び手のスタックを踏んだ)。 */
STATIC_ASSERT(sizeof(struct pci_dev) == PCI_DEV_STRUCT_SIZE, pci_dev_is_40);

static struct pci_dev g_pci[PCI_MAX_DEVS];
static int g_pci_count;      /* 記録した数 */
static int g_pci_seen;       /* 出会った数 (溢れも含む) */
static int g_pci_present;    /* メカニズム #1 が居るか */

/* ======================================================================== */
/*  コンフィギュレーション空間の読み書き                                    */
/* ======================================================================== */
u32 pci_cfg_read32(u32 bus, u32 dev, u32 fn, u32 reg)
{
    if (!g_pci_present) return U32_MAX;
    return pci_config_read32(bus, dev, fn, reg);
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
    if (!g_pci_present) return;
    pci_config_write32(bus, dev, fn, reg, value);
}

/* Command writes must preserve the neighboring W1C Status register. */
int pci_cfg_write16(u32 bus, u32 dev, u32 fn, u32 reg, u16 value)
{
    if (reg & 1u) return OS32_ERR_INVAL;
    if (!g_pci_present) return OS32_ERR_NOSYS;

    pci_config_write16(bus, dev, fn, reg, value);
    return 0;
}

/* ======================================================================== */
/*  1 ファンクションの記録                                                  */
/* ======================================================================== */
static void pci_report(const struct pci_dev *d)
{
    int i;

    /* 実機ではこの行が画面に出る (シリアル越しにも読む)。属性は
     * kprintf の入口で PC-98 流へ直される (.claude/skills/run-os32/SKILL.md#症状別の道具)。 */
    kprintf(ATTR_WHITE, "[pci] %u:%u.%u %04x:%04x class %02x.%02x irq %u pin %u",
            (u32)d->bus, (u32)d->dev, (u32)d->fn,
            (u32)d->vendor, (u32)d->device,
            (u32)d->class, (u32)d->subclass,
            (u32)d->irq_line, (u32)d->irq_pin);

    for (i = 0; i < PCI_CFG_BAR_COUNT; i++) {
        int kind = pci_bar_kind(d->bar[i]);
        if (kind == PCI_BAR_NONE) continue;
        if (kind == PCI_BAR_IO) {
            kprintf(ATTR_WHITE, " bar%d io=0x%x", i, pci_bar_base(d->bar[i]));
        } else {
            kprintf(ATTR_WHITE, " bar%d mem=0x%x", i, pci_bar_base(d->bar[i]));
            if (kind == PCI_BAR_MEM64) i++;
        }
    }
    kprintf(ATTR_WHITE, "\n");
}

static void pci_record(u32 bus, u32 dev, u32 fn, u16 vendor, u32 id_dword)
{
    struct pci_dev *d;
    u32 w;
    int i;

    if (pci_config_bar_measure())
        pci_barsize_record(bus, dev, fn, pci_cfg_read8(bus, dev, fn, PCI_CFG_HEADER_TYPE));
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

    /* Keep public raw BAR records compatible; Type 1 has only two BARs. */
    int bars = pci_header_layout(d->header) == PCI_HDR_LAYOUT_BRIDGE ?
               PCI_BRIDGE_BAR_COUNT : PCI_CFG_BAR_COUNT;
    for (i = 0; i < PCI_CFG_BAR_COUNT; i++) {
        d->bar[i] = i < bars ? pci_cfg_read32(bus, dev, fn,
                           PCI_CFG_BAR0 + PCI_CFG_BAR_STRIDE * i) : 0;
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
    pci_barsize_reset();
    g_pci_count = 0;
    g_pci_seen = 0;
    g_pci_present = 0;

    if (!pci_config_probe()) {
        /* NP21/W はここで止まる。**これが正しい報告**であって失敗ではない。 */
        kprintf(ATTR_CYAN, "[pci] %s absent\n", pci_config_name());
        return 0;
    }
    g_pci_present = 1;
    kprintf(ATTR_GREEN, "[pci] %s ok\n", pci_config_name());

    for (u32 i = 0, bus; i < PCI_CONFIG_MAX_ROOTS && pci_config_root_bus(i, &bus) == 0; i++)
        pci_scan_bus(bus, 0);

    if (g_pci_seen > g_pci_count) {
        kprintf(ATTR_YELLOW, "[pci] %d devices (%d seen, table full)\n",
                g_pci_count, g_pci_seen);
    } else {
        kprintf(ATTR_WHITE, "[pci] %d devices\n", g_pci_count);
    }
    return g_pci_count;
}

int pci_count(void)
{
    return g_pci_count;
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
     * (docs/design/kapi/kapi_db.md#落とし穴 の db_last_error と同じ事故)。 */
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
