/* Public PCI enumeration record; no config I/O or binding declarations. */
#ifndef PCI_DEV_CONTRACT_H
#define PCI_DEV_CONTRACT_H
#include "types.h"

#define PCI_CFG_BAR_COUNT       6

/* 記録の上限。io_pci.md 55〜58 行のとおり PC-98 の bus 0 に載るのは
 * PCMC (dev 0) / C バスブリッヂ (dev 1) / ローカルバスブリッヂ (dev 2) /
 * スロット #0〜2 (dev 8〜10) の数個。ブリッヂ配下を足しても 32 で足りる。
 * 溢れたら記録をやめるだけで、走査は最後まで進む (数は数える)。 */
#define PCI_MAX_DEVS      32

/* ブリッヂを何段まで潜るか。bus 0 が深さ 0。PC-98 の実機構成
 * (ホスト → C バスブリッヂ / ローカルバスブリッヂ) では 1 段で足りるが、
 * PCI-PCI ブリッヂ付きのカードのために 2 段まで見る。**上限があること**が
 * ハングしないことの担保のひとつ (もうひとつは「secondary > 現在のバス」)。 */
#define PCI_MAX_DEPTH     2

#define PCI_DEV_STRUCT_SIZE  40

#endif /* PCI_DEV_CONTRACT_H */

/* Numeric-only consumers must not add types to the HWDEV umbrella early. */
#if !defined(PCI_DEV_CONSTANTS_ONLY) && !defined(PCI_DEV_RECORD_H)
#define PCI_DEV_RECORD_H
/* 1 デバイス (= 1 ファンクション) の記録。
 * **並びを変えたら KAPI の写し (userland/shell/cmd_pci.c) も直すこと。**
 * i386 の既定アラインでちょうど 40 バイト — `pci.c` の STATIC_ASSERT が
 * 見張る (drivers/ata/ide.h の IdeInfo と同じ作法)。 */
struct pci_dev {
    u8  bus;            /*  0 */
    u8  dev;            /*  1 */
    u8  fn;             /*  2 */
    /*  3: padding */
    u16 vendor;         /*  4 */
    u16 device;         /*  6 */
    u8  class;          /*  8  config 0x0B */
    u8  subclass;       /*  9  config 0x0A */
    u8  progif;         /* 10  config 0x09 */
    u8  header;         /* 11  config 0x0E (bit7 込みの生値) */
    u32 bar[PCI_CFG_BAR_COUNT]; /* 12〜35  config 0x10〜0x24 の**生値** */
    u8  irq_line;       /* 36  config 0x3C */
    u8  irq_pin;        /* 37  config 0x3D */
    u16 command;        /* 38  config 0x04 */
};                      /* 40 バイト */

#endif /* PCI_DEV_RECORD_H */
