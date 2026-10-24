/* ======================================================================== */
/*  KPEEK_ALLOW.H — peek -m (MMIO) の許可リスト (TASK_KPEEK §2-5)            */
/*                                                                          */
/*  行 = { vendor, device, bar, off, len, width_mask }。基準番地は実行時に   */
/*  pci_find → pci_get → bar[bar] から取り、I/O BAR・未割当・Memory 無効の   */
/*  BAR は行ごと使わない (K2)。ホストは絶対物理で渡し、要求 [addr, addr+len) */
/*  が 1 行の中に収まり、幅が width_mask に入るときだけ読む。                */
/*                                                                          */
/*  GeForce 6200 (NV44A、10DE:0221、実機 Ra266 の PCI 0:13.0) の BAR0。     */
/*  名前と番地は nouveau の dispnv04/nvreg.h (MIT) と envytools の          */
/*  docs/hw/mmio.rst・rnndb (MIT) — ~/os32-tmp/refs/geforce6200/INDEX.md。 */
/*  PRMVIO = 0x0C0000 + VGA ポート、PRMCIO = 0x601000 + VGA ポート          */
/*  (nvreg.h の NV_PRMVIO0_OFFSET / NV_PRMCIO0_OFFSET と各レジスタの値で確認)。 */
/*  3C9 (DAC データ)・3DA/3BA (INP1、読むと属性 flip-flop が戻る)・PRMDIO は */
/*  読むだけで状態が変わるので行を作らない。                                */
/* ======================================================================== */
#ifndef KDEBUG_KPEEK_ALLOW_H
#define KDEBUG_KPEEK_ALLOW_H

#include "recover.h"

/* width_mask の bit は幅の値そのもの (1 / 2 / 4)。 */
#define KPEEK_W1   1U
#define KPEEK_W2   2U
#define KPEEK_W4   4U

/* PCI BAR の下位 4 bit (メモリ BAR の種別・prefetch)。メモリ BAR だけ扱う。 */
#define KPEEK_BAR_IO_BIT     PCI_BAR_SPACE_IO
#define KPEEK_BAR_ADDR_MASK  PCI_BAR_MEM_ADDR_MASK

/* -k (RAM) で写像や PCD に関わらず断る帯: TVRAM・VRAM・C バス窓・
 * 拡張 ROM (R2)。E8000h 以上の BIOS ROM は PCD でなく読める。 */
#define KPEEK_RAM_DENY_LO    TVRAM_CHAR_BASE   /* 0A0000h */
#define KPEEK_RAM_DENY_HI    BIOS_ROM_BASE     /* 0E8000h (exclusive) */

typedef struct {
    u16 vendor;
    u16 device;
    u8  bar;          /* 0..5 */
    u8  width_mask;   /* KPEEK_W* の和 */
    u16 reserved;
    u32 off;          /* BAR 内オフセット */
    u32 len;          /* バイト数 */
} KpeekAllowRow;

extern const KpeekAllowRow kpeek_allow_rows[];
extern const u32 kpeek_allow_count;

/* [addr, addr + len) が 1 行の中に収まり width がその行で許されていれば
 * KDEBUG_PEEK_OK、無ければ KDEBUG_PEEK_DENY。len >= 1、addr + len は
 * 溢れないこと (呼び手が先に確かめる)。PCI の構成空間は pci_get の写しだけ
 * 読み、装置には触らない。 */
u32 kpeek_allow_check(u32 addr, u32 len, u32 width);

#endif /* KDEBUG_KPEEK_ALLOW_H */
