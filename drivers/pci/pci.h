/* PCI の走査と記録の共通の口。出典 docs/hw の io_pci.md、票 TASK_LAN_82557 §2 L-A。
 * 不在の時 (NP21/W は 0CF8h を持たない) は pci_init が即座に戻り、ハングしないことが実装の条件。 */
/* PCI 設定空間のアクセスと装置の列挙。
 * 後端が示す root bus から走査し、測定が許される場合は BAR の
 * 大きさを測って設定を戻す。設定空間が無い場合の列挙結果は 0 台。 */

#ifndef PCI_H
#define PCI_H

#include "types.h"
#include "os32_kapi_shared.h"   /* OS32_ERR_* (pci_cfg_write16 の戻り値) */
#include "pci_decode.h"

#include "pci_dev_contract.h"
#include "pci_contract.h"

/* ======================================================================== */
/*  コンフィギュレーション空間の読み書き                                    */
/*                                                                          */
/*  いずれも **メカニズム #1 が無ければ**読みは全ビット 1 (= 不在と同じ)、  */
/*  書きは何もしない。                                                      */
/*  0CF8h への書きと 0CFCh の読みのあいだに割り込みが入ると、ハンドラが      */
/*  PCI を触った場合にアドレスが差し替わる。**対は割り込み禁止で囲む**。     */
/* ======================================================================== */
u16 pci_cfg_read16(u32 bus, u32 dev, u32 fn, u32 reg);
u8  pci_cfg_read8(u32 bus, u32 dev, u32 fn, u32 reg);

void pci_cfg_write32(u32 bus, u32 dev, u32 fn, u32 reg, u32 value);

/* Command (04h) だけのための 16 ビット書き。同じ DWORD の上位は Status で
 * **W1C** なので、32 ビットの read-modify-write で代用してはいけない。
 *   0 = 書いた / OS32_ERR_INVAL = reg が奇数 / OS32_ERR_NOSYS = PCI 不在 */
int pci_cfg_write16(u32 bus, u32 dev, u32 fn, u32 reg, u16 value);

/* ======================================================================== */
/*  初期化と参照                                                            */
/* ======================================================================== */

/* vendor:device で探す。見つかれば記録の添字 (>= 0)、無ければ -1。 */
int pci_find(u16 vendor, u16 device);

/* Private kernel/module contract; pci_dev and the public KAPI stay unchanged.
 * reg is a BAR offset or the header-specific ROM offset. len=0: unimplemented
 * or invalid mask. Missing cache entries fail closed; getters never size. */
int pci_bar_get(u32 bus, u32 dev, u32 fn, u32 reg, void *out);
void pci_barsize_reset(void);
void pci_barsize_record(u32 bus, u32 dev, u32 fn, u32 header);

#endif /* PCI_H */
