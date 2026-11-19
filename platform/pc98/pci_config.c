/* PC-98 PCI configuration mechanism #1 (docs/hw/undocumented/io_pci.md). */
#include "pci_config_contract.h"
#include "pci/pci_decode.h"
#include "io.h"

/* ======================================================================== */
/*  メカニズム #1 の有無                                                    */
/*                                                                          */
/*  0CF8h へ既知の値を書いて読み戻す。一致しなければ PCI は無い。           */
/*  **0CF8h は DWORD アクセス必須** (io_pci.md 456 行: バイト/ワードで       */
/*  叩くと通常の I/O アクセスとして扱われ、PCI ではない別のレジスタに       */
/*  当たる)。元の値は書き戻す — PnP BIOS が何か入れたまま残している         */
/*  可能性があるため、検出前のアドレス設定を維持する。                          */
/* ======================================================================== */
int pci_config_probe(void)
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

u32 pci_config_read32(u32 bus, u32 dev, u32 fn, u32 reg)
{
    unsigned int flags;
    u32 val;

    /* アドレスを置いてからデータを読むまでが 1 つの手続き。あいだに
     * 割り込みが入り、ハンドラが PCI を触るとアドレスが差し替わる。 */
    flags = irq_save();
    outpd(PCI_CFG_ADDR_PORT, pci_cfg_addr(bus, dev, fn, reg));
    val = inpd(PCI_CFG_DATA_PORT);
    irq_restore(flags);
    return val;
}

void pci_config_write32(u32 bus, u32 dev, u32 fn, u32 reg, u32 value)
{
    unsigned int flags;

    flags = irq_save();
    outpd(PCI_CFG_ADDR_PORT, pci_cfg_addr(bus, dev, fn, reg));
    outpd(PCI_CFG_DATA_PORT, value);
    irq_restore(flags);
}

/* A word write selects one lane and preserves neighboring W1C Status bits. */
void pci_config_write16(u32 bus, u32 dev, u32 fn, u32 reg, u16 value)
{
    unsigned int flags;

    flags = irq_save();
    outpd(PCI_CFG_ADDR_PORT, pci_cfg_addr(bus, dev, fn, reg));
    outpw(PCI_CFG_DATA_PORT + (reg & PCI_CFG_WORD_LANE_MASK), value);
    irq_restore(flags);
}
