/* ======================================================================== */
/*  PCI_BIND.H — PCI デバイスとドライバの結線表                             */
/*                                                                          */
/*  `pci_init()` が作った列挙表を上から見て、一致した driver の probe を     */
/*  呼ぶだけの層。BAR も IRQ も**割り当てない** (BIOS が付けたものを使う)。 */
/*                                                                          */
/*  ■ 3 つの結果を分ける理由                                                */
/*    probe が失敗したときに「装置に触っていない」のか「未知の状態で        */
/*    残した」のかで、次にできることが違う。触っていなければ次の候補へ      */
/*    渡してよいが、未知の状態の装置を次の driver に渡すと、そちらが        */
/*    「自分が初期化した」と思い込んで DMA を始める。だから                 */
/*    QUARANTINE はその BDF の探索を**打ち切る**。                          */
/*                                                                          */
/*  ■ probe を書く側の契約 (票 §1-4 / 往復 8 R8-1。binder は関与しない)     */
/*    (0) driver は自分の状態を STARTING にしてから装置に触る               */
/*        (IRQ callback と tick フックはその状態を見て装置に触らない)       */
/*    (1) Command の I/O Space Enable → (2) PORT selective reset + 10µs →   */
/*    (3) dma_pool_alloc → (4) irq_register → (5) Bus Master Enable →       */
/*    (6) CU/RU 開始 → (7) **irq_save の短い区間で RUNNING へ**             */
/*    (5) までの失敗は逆順に戻して DECLINE。ただし (2) の reset が Idle を   */
/*    確立できなければ QUARANTINE (状態不明を次へ渡さない)。                */
/*    (6) 以降の失敗は停止の証拠 (§1-3) が取れれば DECLINE、取れなければ    */
/*    QUARANTINE。                                                          */
/*                                                                          */
/*  票  : docs/tasks/v3/TASK_HAL_WIRING.md §1-4                             */
/*  記録: tools/tests/pci_bind_tdd.md                                       */
/* ======================================================================== */

#ifndef PCI_BIND_H
#define PCI_BIND_H

#include "types.h"
#include "pci.h"

/* probe の戻り値 */
#define PCI_PROBE_OK           0
#define PCI_PROBE_DECLINE     -1   /* 触っていない / 完全に戻した → 次の候補へ */
#define PCI_PROBE_QUARANTINE  -2   /* 未知の状態で残した → その BDF を打ち切り */

/* 一致の「任意」。4 欄の AND で、この値の欄は素通し。 */
#define PCI_MATCH_ANY16   0xFFFF
#define PCI_MATCH_ANY8    0xFF

/* BIOS が IRQ を付けていない装置の irq_line (PCI 規格の「未割り当て」)。 */
#define PCI_IRQ_UNASSIGNED 0xFF

struct pci_driver {
    u32 size;         /* sizeof(struct pci_driver)。将来の外部モジュールの版の口 */
    u16 vendor;
    u16 device;
    u8  class;
    u8  subclass;
    const char *name;
    int (*probe)(const struct pci_dev *dev);
};

/* ======================================================================== */
/*  BDF ごとの結果 (往復 8 R8-2)                                            */
/*                                                                          */
/*  `lspci` が [quarantined] などを出すには、probe の戻り値だけでは足りない */
/*  (理由が分からない)。そこで BDF ごとに 8 バイトの記録を残す。            */
/*  **KAPI は足さない** — 外へ出す口は PM が v60 として末尾追記する。       */
/* ======================================================================== */
#define PCI_BIND_NONE        0   /* まだ見ていない / 候補が 1 つも一致しない */
#define PCI_BIND_BOUND       1
#define PCI_BIND_DECLINED    2
#define PCI_BIND_QUARANTINED 3

#define PCI_BIND_OK               0
#define PCI_BIND_NO_DRIVER        1  /* 一致する driver が無い */
#define PCI_BIND_IRQ_UNSUPPORTED  2  /* BIOS が付けた IRQ を共通スタブが持たない */
#define PCI_BIND_IRQ_QUARANTINED  3  /* その線が既に隔離されている */
#define PCI_BIND_RESET_FAILED     4  /* probe (2) の reset が Idle を作れない */
#define PCI_BIND_START_FAILED     5  /* probe (6) 以降で失敗 */
#define PCI_BIND_NOISY            6  /* M の読み戻しが変わらず、線をマスクした */
#define PCI_BIND_NOISY_UNMASKABLE 7  /* 固定 IRQ でマスクできない (未解決の制限) */
#define PCI_BIND_DECLINED_UNSPECIFIED 8  /* DECLINE したが driver が理由を書かなかった */

/* 線の様子。**保存しない** — `pci_bind_info_get()` が読む時点で合成する
 * (往復 9 R1)。結線のときは正常だった線が後から隔離されることがあり、
 * 保存すると `lspci` が古い話をする。合成元は実装 A の
 * `irq_line_quarantined` / `irq_storm_masked` で、PM が合流時に
 * pci_bind_set_line_state_hook() で差す (既定 NULL = LINE_OK)。 */
#define PCI_LINE_OK           0
#define PCI_LINE_STORM_MASKED 1
#define PCI_LINE_QUARANTINED  2   /* 両方立っていればこちら (再起動まで戻らない) */

/* hook が返すビット。**状態そのものではなくビット**で受けるのは、
 * 「隔離とストームのマスクが両方立っている」ときにどちらを名乗るかを
 * hook 側の解釈に任せないため (規則は pci_bind_info_compose が持つ)。
 * 実装 A の `irq_line_quarantined` / `irq_storm_masked` (u16 ビットマスク) を
 * そのまま写すだけの 3 行のアダプタを PM が合流時に書く。 */
#define PCI_LINE_BIT_STORM        0x01
#define PCI_LINE_BIT_QUARANTINED  0x02

struct pci_bind_info {
    u8  bus;
    u8  dev;
    u8  fn;
    u8  result;      /* PCI_BIND_* */
    u8  irq;         /* 列挙時の irq_line (0xFF = 未割り当て) */
    u8  reason;      /* PCI_BIND_OK / … */
    u8  line_state;  /* PCI_LINE_* — **読む時点で合成**。保存値は使わない */
    u8  pad;
};                   /* 8 バイト */

#define PCI_BIND_INFO_SIZE  8

/* 線の様子 (PCI_LINE_BIT_*) を答える関数。実装 A (kernel/irq.c) の持ち物を
 * PM が差す。**ここから kernel/irq.h を include しない** — A と B は別
 * worktree で並行しているので、合流の順序に依存しない形にしてある。 */
typedef u8 (*pci_bind_line_state_fn)(unsigned int irq);
void pci_bind_set_line_state_hook(pci_bind_line_state_fn fn);

/* info->line_state を「いまの線の様子」で上書きし、pad を 0 にする。
 * hook が無ければ PCI_LINE_OK。**irq が 0xFF (未割り当て) か 16 以上なら、
 * シフトも配列参照もする前に** PCI_LINE_OK で戻る (PCI の未割り当て値を
 * そのままシフトすると未定義)。 */
void pci_bind_info_compose(struct pci_bind_info *info);

/* PIC の線の本数。これ以上の irq_line は「線ではない」。 */
#define PCI_BIND_IRQ_MAX  16

/* ======================================================================== */
/*  純粋関数 (drivers/pci_bind_match.c) — I/O を 1 つも出さない             */
/* ======================================================================== */

/* 4 欄の AND。1 = 一致。 */
int pci_bind_match(const struct pci_driver *drv, const struct pci_dev *dev);

/* from 以降で最初に一致する driver の添字。無ければ -1。 */
int pci_bind_next(const struct pci_driver *const *table, int n, int from,
                  const struct pci_dev *dev);

/* 1 台ぶんの候補探索。表順に probe を呼び、
 *   OK         → そこで終わり (BOUND)
 *   DECLINE    → 次の候補へ
 *   QUARANTINE → **その場で打ち切り** (QUARANTINED)
 * 戻り値は最後の probe の戻り値 (一致が 1 つも無ければ PCI_PROBE_DECLINE)。
 * info は result / reason を更新する (上書き規則は pci_bind_match.c の註)。
 * **probe を呼ぶのは呼び手が渡した関数ポインタだけ**なので、ここも純粋
 * (ホスト試験が偽 driver を渡して遷移を全部踏める)。 */
int pci_bind_one(const struct pci_driver *const *table, int n,
                 const struct pci_dev *dev, struct pci_bind_info *info);

/* probe の中から理由を書く口。probe の戻り値だけでは reason が決まらない
 * (DECLINE が「IRQ が無い」なのか「自分の装置ではない」なのか)。
 * いま probe 中の 1 台にだけ効く。 */
void pci_bind_set_reason(u8 reason);

/* pci_bind_one が probe を呼ぶ直前に理由をリセットする内部用 (試験も使う)。 */
void pci_bind_reason_reset(void);
u8   pci_bind_reason_get(void);

/* ======================================================================== */
/*  結線の実行 (drivers/pci_bind.c)                                         */
/* ======================================================================== */

/* 列挙表の全デバイスに対して pci_bind_one を回す。
 * **pgalloc_init と dma_pool_init の後**に呼ぶ (probe が dma_pool_alloc と
 * irq_register を使うので)。戻り値 = BOUND になった台数。 */
int pci_bind_all(const struct pci_driver *const *table, int n);

/* idx 番目 (pci_get と同じ列挙順) の記録を写す。0 = 成功 / -1 = 範囲外。 */
int pci_bind_info_get(int idx, struct pci_bind_info *out);

/* 隔離した台数 (起動後の報告用)。 */
int pci_quarantined(void);

#endif /* PCI_BIND_H */
