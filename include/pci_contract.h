#ifndef PCI_CONTRACT_H
#define PCI_CONTRACT_H

#include "types.h"

u32 pci_cfg_read32(u32 bus, u32 dev, u32 fn, u32 reg);

/* メカニズム #1 の有無を確かめ、あれば bus 0 から列挙する。
 * 戻り値 = 記録したデバイス数 (PCI が無ければ 0)。**再入しても安全**
 * (2 回目以降は表を作り直す)。kernel.c から 1 回だけ呼ぶ。 */
int pci_init(void);

/* 記録したデバイス数 (0〜PCI_MAX_DEVS)。 */
int pci_count(void);

/* idx 番目の記録を out へ写す。戻り値 0 = 成功 / -1 = 範囲外。
 * **内部配列へのポインタは返さない** (docs/design/kapi/kapi_db.md#落とし穴 の
 * `db_last_error` と同じ事故を繰り返さないため — CPL=3 から呼ばれる)。 */
int pci_get(u32 idx, void *out);

#endif /* PCI_CONTRACT_H */
