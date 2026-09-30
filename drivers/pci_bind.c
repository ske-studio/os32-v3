/* ======================================================================== */
/*  PCI_BIND.C — 結線の実行。列挙表を上から回して probe を呼ぶ              */
/*                                                                          */
/*  一致規則と遷移は drivers/pci_bind_match.c (ホスト試験つき)。ここに      */
/*  残るのは「列挙表から 1 台ずつ取り出す」ことと記録と報告だけ。           */
/*                                                                          */
/*  **NP21/W には PCI が無い**ので、この関数は pci_count() == 0 で即座に    */
/*  戻る。実機だけで走る経路なので、[V4] のとおり結果は必ず 1 行出す。      */
/*                                                                          */
/*  票: docs/tasks/v3/TASK_HAL_WIRING.md §1-4                               */
/* ======================================================================== */

#include "pci_bind.h"
#include "kprintf.h"

STATIC_ASSERT(sizeof(struct pci_bind_info) == PCI_BIND_INFO_SIZE,
              pci_bind_info_is_8);

/* 列挙表と同じ順・同じ上限で持つ (pci_get の idx がそのまま使える)。 */
static struct pci_bind_info g_bind[PCI_MAX_DEVS];
static int g_bind_count;
static int g_quarantined;

static const char *pci_bind_result_name(u8 result)
{
    switch (result) {
    case PCI_BIND_BOUND:       return "bound";
    case PCI_BIND_DECLINED:    return "declined";
    case PCI_BIND_QUARANTINED: return "quarantined";
    default:                   return "none";
    }
}

static const char *pci_bind_reason_name(u8 reason)
{
    switch (reason) {
    case PCI_BIND_NO_DRIVER:        return "no-driver";
    case PCI_BIND_IRQ_UNSUPPORTED:  return "irq-unsupported";
    case PCI_BIND_IRQ_QUARANTINED:  return "irq-quarantined";
    case PCI_BIND_RESET_FAILED:     return "reset-failed";
    case PCI_BIND_START_FAILED:     return "start-failed";
    case PCI_BIND_NOISY:            return "noisy";
    case PCI_BIND_NOISY_UNMASKABLE: return "noisy-unmaskable";
    case PCI_BIND_DECLINED_UNSPECIFIED: return "unspecified";
    default:                        return "ok";
    }
}

int pci_bind_all(const struct pci_driver *const *table, int n)
{
    struct pci_dev d;
    int i, count, bound = 0;

    g_bind_count = 0;
    g_quarantined = 0;

    count = pci_count();
    if (count > PCI_MAX_DEVS) count = PCI_MAX_DEVS;

    for (i = 0; i < count; i++) {
        if (pci_get((u32)i, &d) != 0) continue;
        (void)pci_bind_one(table, n, &d, &g_bind[i]);
        g_bind_count = i + 1;

        if (g_bind[i].result == PCI_BIND_BOUND) bound++;
        if (g_bind[i].result == PCI_BIND_QUARANTINED) g_quarantined++;

        /* **黙って終わらない** ([V4])。「一致する driver が無い」は普通の
         * ことなので出さず、実際に触った 2 つだけを 1 行ずつ残す。
         * lspci の注記は PM が KAPI v60 を足すまでこの起動行が代わり。 */
        if (g_bind[i].result == PCI_BIND_BOUND ||
            g_bind[i].result == PCI_BIND_QUARANTINED ||
            (g_bind[i].result == PCI_BIND_DECLINED &&
             g_bind[i].reason != PCI_BIND_OK)) {
            kprintf(0x07, "[pci] %02x:%02x.%x %04x:%04x %s (%s) irq=%02x\n",
                    d.bus, d.dev, d.fn, d.vendor, d.device,
                    pci_bind_result_name(g_bind[i].result),
                    pci_bind_reason_name(g_bind[i].reason),
                    g_bind[i].irq);
        }
    }

    if (count > 0) {
        kprintf(0x07, "[pci] bind: %d bound, %d quarantined of %d\n",
                bound, g_quarantined, count);
    }
    return bound;
}

int pci_bind_info_get(int idx, struct pci_bind_info *out)
{
    if (!out || idx < 0 || idx >= g_bind_count) return -1;
    /* **8 バイトちょうど**を書く。KAPI v60 の wrapper は出力保護
     * (ring3_user_range_writable) をこの大きさで通すので、
     * ここが増えると CPL=3 のバッファをはみ出す (往復 10 R3)。 */
    *out = g_bind[idx];
    /* **保存値ではなく、いまの線の様子**を載せる (往復 9 R1)。 */
    pci_bind_info_compose(out);
    return 0;
}

int pci_quarantined(void)
{
    return g_quarantined;
}
