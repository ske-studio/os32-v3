/* PCI binding and diagnostic types shared by resident consumers. */
#ifndef PCI_BIND_CONTRACT_H
#define PCI_BIND_CONTRACT_H
#include "pci_dev_contract.h"

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

typedef u8 (*pci_bind_line_state_fn)(unsigned int irq);

/* PIC の線の本数。これ以上の irq_line は「線ではない」。 */
#define PCI_BIND_IRQ_MAX  16

#endif /* PCI_BIND_CONTRACT_H */
