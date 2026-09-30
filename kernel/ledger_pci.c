/* ======================================================================== */
/*  LEDGER_PCI.C — PCI の採取値を台帳の資源表へ (TASK_T1_LEDGER §3-3 ⑥-0)    */
/*                                                                          */
/*  pci_init (paging_init より前) は台帳にまだ書けない (Codex B4) ので、     */
/*  g_pci に読んだ生値を pci_bind_all の直前にここで取り込む。レコードは     */
/*  width_basis = RAW (幅未確定) — **予約権限にならない** (ledger_reserve_set */
/*  が RAW を拒否する)。検証済みのレコード (SIZING / DATASHEET / GLUE_CONST) */
/*  は driver か glue が ledger_resource_add で別に登録する。                */
/*                                                                          */
/*  pgalloc.c と分けたのは drivers/pci.h を引くのをこのファイルだけにする    */
/*  ため (pgalloc.c を取り込むホスト試験の探索パスを増やさない)。g_pci は    */
/*  既存の読み口 pci_get (写しを返す) で読む — 列挙の順序も config も触らない。*/
/* ======================================================================== */
#include "pgalloc.h"
#include "pci.h"

/* 記録した各デバイスの**メモリ BAR** (メモリ種別で番地が 0 でないもの) を
 * 1 本ずつ RAW のレコードにする。I/O BAR・生値 0・予約種別は飛ばす。64 ビット
 * BAR は次の BAR が上位 32 ビットなので、上位が 0 のときだけ取り、次の BAR を
 * 飛ばす (4GiB 以上は表せない)。BAR の本数はヘッダの型で Type 0 = 6、
 * ブリッヂ = 2、CardBus = 1、未知の型は 0 (ブリッヂの 18h 以降はバス番号と
 * 窓の設定で BAR ではない)。RAW は幅が未確定なので decode は空 (decode_first ==
 * decode_end = BAR の先頭 PFN)、写像範囲も空。revision は g_pci に無いので 0。
 * 表が溢れた分は ledger_resource_add が ledger_res_overflow に数える。
 * 載せた本数を返す。 */
/* ヘッダの型 (PCI_HDR_LAYOUT_DEVICE / BRIDGE / CARDBUS) ごとの BAR の本数。 */
static const u8 ledger_pci_bars[PCI_HDR_LAYOUT_CARDBUS + 1] = {
    PCI_CFG_BAR_COUNT, 2, 1
};

u32 ledger_resource_import_pci(void)
{
    struct pci_dev d;
    struct ledger_resource r = {0};
    u32 i, b, nb, idx, raw, rid, n;
    int k, layout;
    n = 0;
    for (i = 0; pci_get(i, &d) == 0; i++) {
        layout = pci_header_layout(d.header);
        nb = layout <= PCI_HDR_LAYOUT_CARDBUS ? ledger_pci_bars[layout] : 0;
        for (b = 0; b < nb; b++) {
            idx = b;
            raw = d.bar[b];
            k = pci_bar_kind(raw);
            if (k == PCI_BAR_MEM64 && (b + 1 >= nb || d.bar[++b])) continue;
            if (k < PCI_BAR_MEM32 || k > PCI_BAR_MEM64 ||
                !(raw & PCI_BAR_MEM_ADDR_MASK)) continue;
            r.raw_bar = raw;
            r.decode_first = r.decode_end = (raw & PCI_BAR_MEM_ADDR_MASK) / PAGE_SIZE;
            r.bdf = (u16)(((u32)d.bus << 8) | ((u32)d.dev << 3) | d.fn);
            r.vendor = d.vendor;
            r.device = d.device;
            r.bus = LEDGER_BUS_PCI;
            r.bar = (u8)idx;
            r.width_basis = LEDGER_WB_RAW;
            if (ledger_resource_add(&r, &rid)) n++;
        }
    }
    return n;
}
