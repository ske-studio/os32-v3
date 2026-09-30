/* ======================================================================== */
/*  PCI_DECODE.C — PCI コンフィギュレーションの純粋な復号                    */
/*                                                                          */
/*  ここには副作用のある行を 1 つも置かない。drivers/pci.c から切り出して   */
/*  あるのは、**NP21/W が PCI を実装していない** (`0CF8h` が無い) ために    */
/*  列挙そのものを走らせて確かめられるのが実機 PC-9821Ra266 だけだから。    */
/*    試験: tools/tests/test_pci_decode.py / 記録: tools/tests/pci_decode_tdd.md */
/*                                                                          */
/*  出典: `docs/hw/undocumented/io_pci.md` 図2 (76〜109 行) と 0CF8h の      */
/*        ビット定義 (446〜462 行)、PCI 規格の Type 0 ヘッダ                */
/*        (同じ並びが Intel 8255x SDM Table 1 = テキスト版 697〜713 行)      */
/* ======================================================================== */

#include "pci_decode.h"

/* ======================================================================== */
/*  アドレス語の組み立て (io_pci.md 図2)                                    */
/*    bit31 CONE | bus<<16 | dev<<11 | fn<<8 | (reg & 0xFC)                 */
/* ======================================================================== */
u32 pci_cfg_addr(u32 bus, u32 dev, u32 fn, u32 reg)
{
    /* **それぞれの欄の幅でマスクしてから詰める。** 範囲外の値をそのまま
     * シフトすると隣の欄へ溢れ、まったく別のデバイスの config を読む
     * (dev=32 は bus 欄へ、fn=8 は dev 欄へ落ちる)。列挙の上限を書き
     * 間違えた日に、例外も警告も出ないまま嘘の一覧が出るのが一番困る。
     * reg の bit1〜0 も落とす — PCI アドレスの下位 2 ビットは 00b 固定。 */
    return PCI_CFG_ADDR_ENABLE |
           ((bus & PCI_BUS_MASK) << PCI_CFG_ADDR_BUS_SHIFT) |
           ((dev & PCI_DEV_MASK) << PCI_CFG_ADDR_DEV_SHIFT) |
           ((fn  & PCI_FN_MASK)  << PCI_CFG_ADDR_FN_SHIFT) |
           (reg & PCI_CFG_ADDR_REG_MASK);
}

/* ======================================================================== */
/*  BAR の復号                                                              */
/* ======================================================================== */
int pci_bar_kind(u32 raw)
{
    /* **生値 0 だけ**が「実装されていない」。0x00000001 は
     * 「I/O BAR はあるが番地が割り当てられていない」で、票 §5-2 R1 が
     * 実機で確かめたいのはまさにこの区別 (0 なら自分で割り当てが要る)。 */
    if (raw == 0) return PCI_BAR_NONE;
    if ((raw & PCI_BAR_SPACE_IO) != 0) return PCI_BAR_IO;
    switch ((raw & PCI_BAR_MEM_TYPE) >> 1) {
    case 0: return PCI_BAR_MEM32;   /* 00b: 32 ビット空間のどこでも */
    case 1: return PCI_BAR_MEM1M;   /* 01b: 1MB 未満 (旧式) */
    case 2: return PCI_BAR_MEM64;   /* 10b: 64 ビット (次の BAR が上位) */
    default: break;                 /* 11b: 予約 */
    }
    return PCI_BAR_MEMRSV;
}

int pci_bar_prefetchable(u32 raw)
{
    /* I/O BAR では bit3 は**番地の一部**。無条件に見ると I/O 窓
     * 0xE808 が「prefetchable」という存在しないものに化ける。 */
    if (raw == 0) return 0;
    if ((raw & PCI_BAR_SPACE_IO) != 0) return 0;
    return (raw & PCI_BAR_MEM_PREFETCH) ? 1 : 0;
}

u32 pci_bar_base(u32 raw)
{
    if (raw == 0) return 0;
    /* I/O は下位 2 ビットだけ (~3)。~0xF で切ると 0xE808 が 0xE800 に
     * なる — 8255x の I/O 窓は 32 バイト境界なので実機で外す形。 */
    if ((raw & PCI_BAR_SPACE_IO) != 0) return raw & PCI_BAR_IO_ADDR_MASK;
    return raw & PCI_BAR_MEM_ADDR_MASK;
}

/* ======================================================================== */
/*  名前引き (表示だけ。判断には使わない)                                   */
/* ======================================================================== */
const char *pci_class_name(u8 cls, u8 sub)
{
    switch (cls) {
    case PCI_CLASS_STORAGE:
        if (sub == 0x00) return "Storage/SCSI";
        if (sub == 0x01) return "Storage/IDE";
        return "Storage";
    case PCI_CLASS_NETWORK:
        if (sub == PCI_SUB_NET_ETHERNET) return "Network/Ethernet";
        return "Network";
    case PCI_CLASS_DISPLAY:
        if (sub == 0x00) return "Display/VGA";
        return "Display";
    case PCI_CLASS_MULTIMEDIA:
        /* TV チューナー / キャプチャ (GV-MVP/HX2 など) はここに出る。 */
        if (sub == 0x00) return "Multimedia/Video";
        if (sub == 0x01) return "Multimedia/Audio";
        return "Multimedia";
    case PCI_CLASS_BRIDGE:
        if (sub == PCI_SUB_BRIDGE_HOST) return "Bridge/Host";
        if (sub == PCI_SUB_BRIDGE_ISA) return "Bridge/ISA";
        if (sub == PCI_SUB_BRIDGE_PCI) return "Bridge/PCI";
        return "Bridge";
    default:
        break;
    }
    /* 空欄にしない — `lspci` の欄がずれて読めなくなる。 */
    return "Unknown";
}

const char *pci_vendor_name(u16 vendor)
{
    /* io_pci.md 271〜280 行 (Na9/Na12 の内訳) と 336〜345 行 (PC-98 用
     * PCI ボードの表) に出るベンダだけ。表示の補助なので、知らなければ
     * 空文字でよい — 行には vendor:device の 16 進が必ず出る。 */
    switch (vendor) {
    case 0x8086: return "Intel";
    case 0x1033: return "NEC";
    case 0x102B: return "Matrox";
    case 0x9004: return "Adaptec";
    case 0x1023: return "Trident";
    case 0x1013: return "Cirrus";
    /* ここから下は io_pci.md の表には無い。PCI の TV チューナー
     * (I-O DATA GV-MVP/HX2 など) を `lspci -v` で識別するときに、
     * カード自身か搭載チップのどちらのベンダが出ても名前が付くように。 */
    case 0x10FC: return "I-O DATA";
    case 0x14F1: return "Conexant";
    case 0x1131: return "Philips";
    case 0x109E: return "Brooktree";
    default: break;
    }
    return "";
}

int pci_is_multifunction(u8 header_type)
{
    return (header_type & PCI_HDR_MULTIFUNCTION) ? 1 : 0;
}

int pci_header_layout(u8 header_type)
{
    /* **bit7 を落としてから比べる。** 落とさないとマルチファンクションの
     * ブリッヂ (0x81) が Type 0 扱いになり、secondary bus を読まずに
     * 配下のバスを丸ごと見落とす。 */
    return (int)(header_type & PCI_HDR_LAYOUT_MASK);
}

/* ======================================================================== */
/*  DWORD からの切り出し                                                    */
/* ======================================================================== */
u8 pci_extract8(u32 dword, u32 reg)
{
    return (u8)(dword >> ((reg & 3) * 8));
}

u16 pci_extract16(u32 dword, u32 reg)
{
    /* u16 の欄は PCI では必ず偶数オフセットに置かれ、バイト境界を
     * またがない。マスクを 3 にすると奇数オフセットで 8 ビットずれた値を
     * 返し、Vendor ID と Device ID が混ざる。 */
    return (u16)(dword >> ((reg & 2) * 8));
}
