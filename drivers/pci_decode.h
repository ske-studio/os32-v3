/* ======================================================================== */
/*  PCI_DECODE.H — PCI コンフィギュレーションの純粋な復号                    */
/*                                                                          */
/*  drivers/pci.c から「ビットをどう読むか」だけを切り出したもの。          */
/*  ポートも静的配列も構造体の padding も触らないので、**ホストでそのまま**  */
/*  **試験できる**。切り出した理由は、PCI が                                */
/*    **NP21/W に存在しない** (`0CF8h` が実装されていない) ため、            */
/*  走らせて確かめられるのが実機 PC-9821Ra266 だけだから。机上の判断のまま   */
/*  実機へ持って行くと、1 回の実機セッションを復号の間違いで潰す。           */
/*                                                                          */
/*  試験: tools/tests/pci_decode_host.c + tools/tests/test_pci_decode.py    */
/*  記録: tools/tests/pci_decode_tdd.md                                    */
/*  票  : docs/tasks/realhw/TASK_LAN_82557.md §2 L-A                       */
/*                                                                          */
/*  出典:                                                                   */
/*    - `docs/hw/undocumented/io_pci.md` 76〜109 行 図2 (メカニズム #1       */
/*      Type0 のアドレス語)、111〜141 行 図3 (Type1)、446〜462 行           */
/*      (0CF8h CONFIGURATION ADDRESS REGISTER のビット定義)                 */
/*    - PCI Local Bus Specification の Type 0 コンフィギュレーションヘッダ。 */
/*      同じ並びが Intel 8255x SDM の Table 1 (テキスト版 697〜713 行) に    */
/*      82557 の実装として載っている — こちらが手元で引ける正典             */
/* ======================================================================== */

#ifndef PCI_DECODE_H
#define PCI_DECODE_H

#include "types.h"

/* ======================================================================== */
/*  1. コンフィギュレーションアドレスレジスタ (I/O 0CF8h、DWORD)            */
/*                                                                          */
/*  io_pci.md 446〜456 行:                                                  */
/*    bit 31    : Configuration Enable (CONE) — 1 でメカニズム #1           */
/*    bit 30〜24: Reserved (常に 0)                                         */
/*    bit 23〜16: Bus Number                                                */
/*    bit 15〜11: Device Number   (図2 の XXXXX)                            */
/*    bit 10〜 8: Function Number (図2 の YYY)                              */
/*    bit  7〜 2: Register Offset (図2 の ZZZZZZ)                           */
/*    bit  1〜 0: 常に 0 (PCI アドレスの下位 2 ビットに落ちる)              */
/* ======================================================================== */
#define PCI_CFG_ADDR_PORT       0x0CF8  /* io_pci.md 446 行。**DWORD 必須** */
#define PCI_CFG_DATA_PORT       0x0CFC  /* io_pci.md 468 行 */

#define PCI_CFG_ADDR_ENABLE     0x80000000UL  /* bit31 CONE */
#define PCI_CFG_ADDR_BUS_SHIFT  16
#define PCI_CFG_ADDR_DEV_SHIFT  11
#define PCI_CFG_ADDR_FN_SHIFT   8
#define PCI_CFG_ADDR_REG_MASK   0xFCUL        /* bit7〜2 (下位 2 ビットは 0) */

#define PCI_BUS_MASK            0xFFUL        /* bit23〜16 = 8 ビット */
#define PCI_DEV_MASK            0x1FUL        /* bit15〜11 = 5 ビット (0〜31) */
#define PCI_FN_MASK             0x07UL        /* bit10〜 8 = 3 ビット (0〜7) */

#define PCI_DEV_COUNT           32            /* 1 バスあたりのデバイス数 */
#define PCI_FN_COUNT            8             /* 1 デバイスあたりのファンクション数 */

/* 「PCI が居るか」を確かめるときに 0CF8h へ書いて読み戻す値。
 * bit31 (CONE) と bit23〜2 の**書き込める全ビット**を使う 2 本立てにする。
 * 0x80000000 だけだと、0CF8h が素通しの 32 ビットラッチである機械
 * (io_pci.md 458 行: bit31=0 のとき 0CF8h〜0CFAh はマーキュリー互換の
 * 別レジスタ) を PCI と誤認し得る。逆に bit1〜0 を立てた値は**本物の**
 * メカニズム #1 でも 0 に落ちて読み戻せないので、ここには入れない。 */
#define PCI_MECH1_PROBE_A       0x80000000UL
#define PCI_MECH1_PROBE_B       0x80FFFFFCUL  /* bus 0xFF / dev 31 / fn 7 / reg 0xFC */

/* 組み立て: bit31 | bus<<16 | dev<<11 | fn<<8 | (reg & 0xFC)。
 * bus / dev / fn / reg は呼び手が範囲外を渡しても**隣の欄を侵さない**よう
 * マスクしてから詰める (侵すと別のデバイスの config を読む)。 */
u32 pci_cfg_addr(u32 bus, u32 dev, u32 fn, u32 reg);

/* ======================================================================== */
/*  2. Type 0 コンフィギュレーションヘッダのオフセット                      */
/*                                                                          */
/*  8255x SDM Table 1 (697〜713 行) と PCI 規格。                           */
/* ======================================================================== */
#define PCI_CFG_VENDOR_ID       0x00  /* u16。0xFFFF = デバイス不在 */
#define PCI_CFG_DEVICE_ID       0x02  /* u16 */
#define PCI_CFG_COMMAND         0x04  /* u16 */
#define PCI_CFG_STATUS          0x06  /* u16 */
#define PCI_CFG_REVISION_ID     0x08  /* u8  */
#define PCI_CFG_PROG_IF         0x09  /* u8  */
#define PCI_CFG_SUBCLASS        0x0A  /* u8  */
#define PCI_CFG_CLASS           0x0B  /* u8  */
#define PCI_CFG_CACHE_LINE      0x0C  /* u8  */
#define PCI_CFG_LATENCY_TIMER   0x0D  /* u8  */
#define PCI_CFG_HEADER_TYPE     0x0E  /* u8。bit7 = マルチファンクション */
#define PCI_CFG_BIST            0x0F  /* u8  */
#define PCI_CFG_BAR0            0x10  /* u32 × 6 (0x10〜0x24) */
#define PCI_CFG_BAR_COUNT       6
#define PCI_CFG_PRIMARY_BUS     0x18  /* u8 (Type 1 のみ) */
#define PCI_CFG_SECONDARY_BUS   0x19  /* u8 (Type 1 のみ) */
#define PCI_CFG_SUBORDINATE_BUS 0x1A  /* u8 (Type 1 のみ) */
#define PCI_CFG_SUBSYS_VENDOR   0x2C  /* u16 */
#define PCI_CFG_SUBSYS_ID       0x2E  /* u16 */
#define PCI_CFG_CAP_PTR         0x34  /* u8 (SDM 4.1.14) */
#define PCI_CFG_INT_LINE        0x3C  /* u8 (SDM 4.1.15) */
#define PCI_CFG_INT_PIN         0x3D  /* u8 (SDM 4.1.16、82557 は常に 1 = INTA#) */
#define PCI_CFG_SPACE_SIZE      256   /* config 空間 1 ファンクション分 */

/* Command レジスタ (0x04) のビット。L-B で I/O Enable / Bus Master が
 * 立っているかを見るために名前を付けておく (列挙では**書かない**)。 */
#define PCI_CMD_IO_ENABLE       0x0001
#define PCI_CMD_MEM_ENABLE      0x0002
#define PCI_CMD_BUS_MASTER      0x0004

/* Header Type (0x0E) */
#define PCI_HDR_MULTIFUNCTION   0x80  /* bit7 */
#define PCI_HDR_LAYOUT_MASK     0x7F  /* bit6〜0 */
#define PCI_HDR_LAYOUT_DEVICE   0x00  /* Type 0 (通常のデバイス) */
#define PCI_HDR_LAYOUT_BRIDGE   0x01  /* Type 1 (PCI-PCI ブリッヂ) */
#define PCI_HDR_LAYOUT_CARDBUS  0x02  /* Type 2 */

/* クラスコード (0x0B / 0x0A)。io_pci.md と PCI 規格。 */
#define PCI_CLASS_STORAGE       0x01
#define PCI_CLASS_NETWORK       0x02
#define PCI_CLASS_DISPLAY       0x03
#define PCI_CLASS_MULTIMEDIA    0x04
#define PCI_CLASS_BRIDGE        0x06
#define PCI_SUB_NET_ETHERNET    0x00
#define PCI_SUB_BRIDGE_HOST     0x00
#define PCI_SUB_BRIDGE_ISA      0x01
#define PCI_SUB_BRIDGE_PCI      0x04

/* 狙いのデバイス (票 §2 L-A)。8255x SDM 4.1.1 / 4.1.2 (727〜736 行)。 */
#define PCI_VENDOR_INTEL        0x8086
#define PCI_DEVICE_82557        0x1229

/* 不在マーカ。応答が無いバスサイクルは全ビット 1 で返る。 */
#define PCI_VENDOR_NONE         0xFFFF

/* ======================================================================== */
/*  3. BAR (Base Address Register) の復号                                   */
/*                                                                          */
/*  bit0 = 1 なら I/O 空間 (番地は bit31〜2)、0 ならメモリ空間 (番地は       */
/*  bit31〜4)。メモリのとき bit2〜1 が型、bit3 が prefetchable。            */
/*                                                                          */
/*  **大きさは取らない。** 取るには全 1 を書いて読み戻す必要があり、         */
/*  BIOS が割り当てた番地を一瞬でも壊す。票 §5-2 が実機で欲しいのは         */
/*  「BIOS が割り当て済みか」= 生値そのものなので、読むだけで足りる。       */
/* ======================================================================== */
#define PCI_BAR_NONE    0   /* 生値 0 = そのレジスタは実装されていない */
#define PCI_BAR_IO      1   /* bit0 = 1 */
#define PCI_BAR_MEM32   2   /* bit0 = 0、bit2〜1 = 00b */
#define PCI_BAR_MEM1M   3   /* bit0 = 0、bit2〜1 = 01b (1MB 未満、旧式) */
#define PCI_BAR_MEM64   4   /* bit0 = 0、bit2〜1 = 10b (次の BAR が上位 32 ビット) */
#define PCI_BAR_MEMRSV  5   /* bit0 = 0、bit2〜1 = 11b (予約) */

#define PCI_BAR_SPACE_IO    0x00000001UL  /* bit0 */
#define PCI_BAR_MEM_TYPE    0x00000006UL  /* bit2〜1 */
#define PCI_BAR_MEM_PREFETCH 0x00000008UL /* bit3 */
#define PCI_BAR_IO_ADDR_MASK  0xFFFFFFFCUL /* ~3 */
#define PCI_BAR_MEM_ADDR_MASK 0xFFFFFFF0UL /* ~0xF */

/* 生値から種別 (PCI_BAR_*) を返す。
 * **生値 0 だけ**を PCI_BAR_NONE にする。0x00000001 (= 番地未割り当ての
 * I/O BAR) は NONE ではなく IO で、番地 0 — 票 §5-2 R1 が実機で見たいのは
 * まさにこの区別 (「0 なら自分で割り当てが要る」)。 */
int pci_bar_kind(u32 raw);

/* メモリ BAR の prefetchable (bit3)。I/O BAR では bit3 は番地の一部なので
 * **常に 0 を返す** — ここを見分けないと I/O 番地 0xE808 が
 * 「prefetchable」に化ける。 */
int pci_bar_prefetchable(u32 raw);

/* 生値から番地を取り出す。I/O は ~3、メモリは ~0xF。
 * 生値 0 は 0。 */
u32 pci_bar_base(u32 raw);

/* ======================================================================== */
/*  4. 名前引き (表示のためだけ。判断には使わない)                          */
/* ======================================================================== */

/* クラス/サブクラスの短い名前。知らない組み合わせは "" ではなく
 * "Unknown" を返す (表示が空欄になると欄がずれる)。 */
const char *pci_class_name(u8 cls, u8 sub);

/* ベンダ ID の短い名前。io_pci.md 271〜280 行 / 336〜345 行の表にある
 * PC-98 で実際に出るベンダだけ。知らなければ "" (空文字) — こちらは
 * `lspci` の行で vendor:device の 16 進が既に出ているので欄は崩れない。 */
const char *pci_vendor_name(u16 vendor);

/* Header Type bit7。fn0 を読んだあと fn1〜7 を走査するかの判定。 */
int pci_is_multifunction(u8 header_type);

/* Header Type bit6〜0 (PCI_HDR_LAYOUT_*)。 */
int pci_header_layout(u8 header_type);

/* DWORD 読みから 8 / 16 ビットを切り出す。`reg` は**そろえていない**
 * 元のオフセット (例 0x3D)、`dword` はその DWORD (reg & 0xFC) の生値。
 * 0CFCh はバイト/ワードアクセスも許されている (io_pci.md 474 行) が、
 * **0CF8h が DWORD 必須**なのでアドレスは常に DWORD で置く。
 * 読みも DWORD に揃えて切り出す方が、機種ごとのバイトレーン配線の違いを
 * 踏まない。 */
u8  pci_extract8(u32 dword, u32 reg);
u16 pci_extract16(u32 dword, u32 reg);

#endif /* PCI_DECODE_H */
