/* ======================================================================== */
/*  PCI_BIND_MATCH.C — 一致規則と候補探索だけ。I/O を 1 つも出さない        */
/*                                                                          */
/*  probe は呼び手が渡した関数ポインタなので、ホスト試験が偽 driver を      */
/*  並べて DECLINE → 次へ / QUARANTINE → 打ち切り の遷移を全部踏める        */
/*  (tools/tests/test_pci_bind.py)。**NP21/W には PCI が無い**ので、        */
/*  この層はエミュレータでは 1 行も走らない — だからホストで固定する。      */
/*                                                                          */
/*  票  : docs/tasks/v3/TASK_HAL_WIRING.md §1-4                             */
/*  記録: tools/tests/pci_bind_tdd.md                                       */
/* ======================================================================== */

#include "pci_bind.h"

/* いま probe 中の 1 台ぶんの理由。probe の戻り値だけでは
 * 「IRQ が無くて降りた」のか「自分の装置ではない」のかが分からない。 */
static u8 s_reason = PCI_BIND_OK;

void pci_bind_set_reason(u8 reason)
{
    s_reason = reason;
}

void pci_bind_reason_reset(void)
{
    s_reason = PCI_BIND_OK;
}

u8 pci_bind_reason_get(void)
{
    return s_reason;
}

/* ------------------------------------------------------------------------ */
/*  線の様子は**読む時点で合成する** (往復 9 R1)                            */
/*                                                                          */
/*  結線のときは正常だった線が、後から noisy 隔離やストームのマスクに入る。 */
/*  保存すると `lspci` が古い話をするので、保存せずここで引く。             */
/* ------------------------------------------------------------------------ */
static pci_bind_line_state_fn s_line_state_hook = (pci_bind_line_state_fn)0;

void pci_bind_set_line_state_hook(pci_bind_line_state_fn fn)
{
    s_line_state_hook = fn;
}

void pci_bind_info_compose(struct pci_bind_info *info)
{
    u8 bits;

    if (!info) return;
    info->line_state = PCI_LINE_OK;
    info->pad = 0;
    /* **範囲の検査をシフト・配列参照より先に**。0xFF = BIOS が IRQ を
     * 付けていない (PCI 規格の未割り当て)。16 以上も線ではない。 */
    if (info->irq == PCI_IRQ_UNASSIGNED) return;
    if (info->irq >= PCI_BIND_IRQ_MAX) return;
    if (!s_line_state_hook) return;

    bits = s_line_state_hook((unsigned int)info->irq);
    /* **隔離が勝つ。** 隔離は再起動まで戻らないが、ストームのマスクは
     * 登録数の再計算で外れ得る — 弱いほうを名乗ると復旧済みに見える。 */
    if (bits & PCI_LINE_BIT_QUARANTINED)  info->line_state = PCI_LINE_QUARANTINED;
    else if (bits & PCI_LINE_BIT_STORM)   info->line_state = PCI_LINE_STORM_MASKED;
}

/* ------------------------------------------------------------------------ */
/*  4 欄の AND。0xFFFF / 0xFF は任意。                                      */
/*                                                                          */
/*  **同じ driver が複数の装置 (別 BDF) に一致してよい** — 2 台目を         */
/*  DECLINE するか自分で複数を持つかは driver の判断。binder は数えない。   */
/* ------------------------------------------------------------------------ */
int pci_bind_match(const struct pci_driver *drv, const struct pci_dev *dev)
{
    if (!drv || !dev) return 0;
    if (drv->vendor != PCI_MATCH_ANY16 && drv->vendor != dev->vendor) return 0;
    if (drv->device != PCI_MATCH_ANY16 && drv->device != dev->device) return 0;
    if (drv->class != PCI_MATCH_ANY8 && drv->class != dev->class) return 0;
    if (drv->subclass != PCI_MATCH_ANY8 && drv->subclass != dev->subclass)
        return 0;
    return 1;
}

int pci_bind_next(const struct pci_driver *const *table, int n, int from,
                  const struct pci_dev *dev)
{
    int i;

    if (!table || !dev || n <= 0) return -1;
    if (from < 0) from = 0;
    for (i = from; i < n; i++) {
        if (!table[i]) continue;
        if (pci_bind_match(table[i], dev)) return i;
    }
    return -1;
}

/* ------------------------------------------------------------------------ */
/*  1 台ぶんの候補探索                                                      */
/*                                                                          */
/*  記録の上書き規則 (往復 8 R8-2):                                         */
/*    - DECLINE の後に別 driver が BOUND になれば BOUND/OK で上書きする     */
/*    - 候補が全部 DECLINE なら **最後の** DECLINED と理由を残す            */
/*    - QUARANTINED は上書きされない (そこで探索を打ち切るので、後続の      */
/*      probe がそもそも走らない)                                           */
/*    - 一致が 1 つも無ければ NONE / NO_DRIVER                              */
/* ------------------------------------------------------------------------ */
int pci_bind_one(const struct pci_driver *const *table, int n,
                 const struct pci_dev *dev, struct pci_bind_info *info)
{
    int idx, rc;

    if (!dev) return PCI_PROBE_DECLINE;

    if (info) {
        info->bus = dev->bus;
        info->dev = dev->dev;
        info->fn = dev->fn;
        info->irq = dev->irq_line;
        info->result = PCI_BIND_NONE;
        info->reason = PCI_BIND_NO_DRIVER;
        info->line_state = PCI_LINE_OK;
        info->pad = 0;
    }

    rc = PCI_PROBE_DECLINE;
    idx = pci_bind_next(table, n, 0, dev);
    while (idx >= 0) {
        if (!table[idx]->probe) {
            idx = pci_bind_next(table, n, idx + 1, dev);
            continue;
        }
        pci_bind_reason_reset();
        rc = table[idx]->probe(dev);

        if (rc == PCI_PROBE_OK) {
            if (info) {
                info->result = PCI_BIND_BOUND;
                info->reason = PCI_BIND_OK;
            }
            return rc;
        }
        if (rc == PCI_PROBE_QUARANTINE) {
            /* **打ち切る。** 状態不明の装置を次の driver に渡さない。 */
            if (info) {
                info->result = PCI_BIND_QUARANTINED;
                info->reason = (s_reason != PCI_BIND_OK) ? s_reason
                                                         : PCI_BIND_RESET_FAILED;
            }
            return rc;
        }
        /* DECLINE (およびそれ以外の負値) → 次の候補へ。
         * **理由は候補ごとに OK へ初期化してある**ので、前の候補の理由が
         * 後の候補に残らない (往復 9 R2)。書かなかった driver の DECLINE は
         * 「理由不明」であって「問題なし」ではない。 */
        if (info) {
            info->result = PCI_BIND_DECLINED;
            info->reason = (s_reason != PCI_BIND_OK) ? s_reason
                                          : PCI_BIND_DECLINED_UNSPECIFIED;
        }
        idx = pci_bind_next(table, n, idx + 1, dev);
    }
    return rc;
}
