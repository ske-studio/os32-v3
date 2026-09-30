/* ======================================================================== */
/*  PCI.H — PCI コンフィギュレーションメカニズム #1 と列挙                   */
/*                                                                          */
/*  PC-9821 の PCI バスを I/O 0CF8h / 0CFCh (メカニズム #1) で走査し、       */
/*  見つけたデバイスを静的な表に記録する。**読むだけ** — BAR のサイズ判定    */
/*  (全 1 を書いて読み戻す) も割り当てもしない。BIOS が付けた番地を壊さない  */
/*  ことが、この段 (票 L-A) の唯一の仕事だから。                            */
/*                                                                          */
/*  ■ NP21/W には PCI が無い                                                */
/*    エミュレータは 0CF8h を実装していないので、`pci_init()` は             */
/*    「メカニズム #1 が無い」と報告して即座に戻る。**絶対にハングしない**   */
/*    こと (読み戻しが一致しなければその場で諦める) が実装の条件。           */
/*                                                                          */
/*  ■ 分解した部品                                                          */
/*    ビットの読み方 (アドレス語の組み立て・BAR の復号・ヘッダの切り出し)    */
/*    は drivers/pci_decode.c に切り出してホストで試験してある。ここに残る   */
/*    のは I/O と走査順と記録だけ。                                         */
/*                                                                          */
/*  票  : docs/tasks/realhw/TASK_LAN_82557.md §2 L-A                        */
/*  出典: docs/hw/undocumented/io_pci.md (PC-98 の PCI)、PCI 規格            */
/* ======================================================================== */

#ifndef PCI_H
#define PCI_H

#include "types.h"
#include "os32_kapi_shared.h"   /* OS32_ERR_* (pci_cfg_write16 の戻り値) */
#include "pci_decode.h"

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

/* 1 デバイス (= 1 ファンクション) の記録。
 * **並びを変えたら KAPI の写し (userland/shell/cmd_pci.c) も直すこと。**
 * i386 の既定アラインでちょうど 40 バイト — `pci.c` の STATIC_ASSERT が
 * 見張る (drivers/ide.h の IdeInfo と同じ作法)。 */
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

#define PCI_DEV_STRUCT_SIZE  40

/* ======================================================================== */
/*  コンフィギュレーション空間の読み書き                                    */
/*                                                                          */
/*  いずれも **メカニズム #1 が無ければ**読みは全ビット 1 (= 不在と同じ)、  */
/*  書きは何もしない。呼び手が毎回 pci_present() を確かめなくてよいように。  */
/*  0CF8h への書きと 0CFCh の読みのあいだに割り込みが入ると、ハンドラが      */
/*  PCI を触った場合にアドレスが差し替わる。**対は割り込み禁止で囲む**。     */
/* ======================================================================== */
u32 pci_cfg_read32(u32 bus, u32 dev, u32 fn, u32 reg);
u16 pci_cfg_read16(u32 bus, u32 dev, u32 fn, u32 reg);
u8  pci_cfg_read8(u32 bus, u32 dev, u32 fn, u32 reg);

/* 書きは列挙では**使わない**。L-B (82557 ドライバ) が Command レジスタの
 * Bus Master を立てるために要るので口だけ開けておく。BAR には書かない。 */
void pci_cfg_write32(u32 bus, u32 dev, u32 fn, u32 reg, u32 value);

/* Command (04h) だけのための 16 ビット書き。同じ DWORD の上位は Status で
 * **W1C** なので、32 ビットの read-modify-write で代用してはいけない。
 *   0 = 書いた / OS32_ERR_INVAL = reg が奇数 / OS32_ERR_NOSYS = PCI 不在 */
int pci_cfg_write16(u32 bus, u32 dev, u32 fn, u32 reg, u16 value);

/* ======================================================================== */
/*  初期化と参照                                                            */
/* ======================================================================== */

/* メカニズム #1 の有無を確かめ、あれば bus 0 から列挙する。
 * 戻り値 = 記録したデバイス数 (PCI が無ければ 0)。**再入しても安全**
 * (2 回目以降は表を作り直す)。kernel.c から 1 回だけ呼ぶ。 */
int pci_init(void);

/* メカニズム #1 が見つかったか。pci_init() より前は 0。 */
int pci_present(void);

/* 記録したデバイス数 (0〜PCI_MAX_DEVS)。 */
int pci_count(void);

/* 走査で**出会った**ファンクション数。PCI_MAX_DEVS を超えていたら
 * pci_count() より大きくなる (記録が溢れたことが分かる)。 */
int pci_seen(void);

/* idx 番目の記録を out へ写す。戻り値 0 = 成功 / -1 = 範囲外。
 * **内部配列へのポインタは返さない** (POLICY_DEBUG §4-13 の
 * `db_last_error` と同じ事故を繰り返さないため — CPL=3 から呼ばれる)。 */
int pci_get(u32 idx, void *out);

/* vendor:device で探す。見つかれば記録の添字 (>= 0)、無ければ -1。 */
int pci_find(u16 vendor, u16 device);

#endif /* PCI_H */
