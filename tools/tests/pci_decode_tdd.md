# PCI コンフィギュレーションの復号 — 実機でしか走らせられない層のホスト試験 (記録)

- 票: [`docs/tasks/realhw/TASK_LAN_82557.md`](../../docs/tasks/realhw/TASK_LAN_82557.md) §2 **L-A** /
  §5-1 の「手元で済むこと」の 1 行目
- 目的: 実機 **PC-9821Ra266** の内蔵 LAN (Intel 82557 = `8086:1229`) を `lspci` で見つけ、
  BAR と Interrupt Line を報告する。ドライバ (L-B) はまだ書かない
- 対象: [`drivers/pci.c`](../../drivers/pci.c) / [`drivers/pci.h`](../../drivers/pci.h) と、
  そこから切り出した純粋な復号 [`drivers/pci_decode.c`](../../drivers/pci_decode.c)
- 実行: `python3 -B tools/tests/test_pci_decode.py [--target] [--mutate] [case ...]`
  (`make check-pci-decode-host` が `--target --mutate` 付きで回す)
- 試験: [`pci_decode_host.c`](pci_decode_host.c) — 実物の `drivers/pci_decode.c` を
  1 行も写さずに `#include` する。復号は I/O も静的配列も構造体の padding も
  触らないので **模型が 1 つも要らない**
- KAPI: **v56 → v57**。`pci_count` / `pci_get` / `pci_cfg_read32` の 3 本を末尾に追記 ([ABI2])

## 1. なぜエミュレータで見つけられないか

**NP21/W は PCI を実装していない。** `0CF8h` / `0CFCh` のハンドラが無く、
`np21w-src` には PCI コンフィギュレーション空間そのものが存在しない。
だから L-A で書いたコードのうち、エミュレータで踏めるのは

> 「メカニズム #1 の読み戻しが一致しない → `[pci] mech#1 absent` と出して戻る」

の **1 本道だけ**で、アドレス語の組み立ても BAR の復号も 1 ビットも実行されない。

一方、実機の 1 回は安くない。会話はシリアル 115200 (3.2 KB/s、
`tools/rshell_serial.py`) で、電源を入れて `lspci` を打つまでに手が要る。
ビットの読み違いで 1 回を潰すと、票 §5-2 の R1〜R4 (I/O 窓が割り当て済みか、
PIRQ が何番に落ちているか) がまるごと次回送りになる。

そこで **判断を含むビット操作を全部 `pci_decode.c` に集め**、ここで潰す。
`drivers/pci.c` に残したのは I/O と走査順と記録だけで、判断は 1 つも無い。

## 2. RED → GREEN

先に `pci_decode_host.c` の 9 ケースを書き、「資料を素直に読むと書いてしまう形」の
実装を置いて回した。**9 本中 6 本が RED** (2026-09-22)。

```
HOST GNU89 -Werror compile PASS (real drivers/pci_decode.c)
FAIL cfg_addr:53: pci_cfg_addr(0, 32, 0, 0) == 0x80000000UL
FAIL cfg_addr:54: pci_cfg_addr(0, 0, 8, 0) == 0x80000000UL
FAIL cfg_addr:55: pci_cfg_addr(256, 0, 0, 0) == 0x80000000UL
FAIL cfg_addr:57: (pci_cfg_addr(256, 32, 8, 0xFF) & 0x7F000000UL) == 0
FAIL bar_kind:100: pci_bar_kind(0x00000001UL) == PCI_BAR_IO
FAIL bar_base:121: pci_bar_base(0x0000E809UL) == 0x0000E808UL
FAIL bar_base:139: pci_bar_prefetchable(0x0000E809UL) == 0
FAIL bar_base:140: pci_bar_prefetchable(0x0000E80DUL) == 0
FAIL header_type:159: pci_header_layout(0x80) == PCI_HDR_LAYOUT_DEVICE
FAIL header_type:161: pci_header_layout(0x81) == PCI_HDR_LAYOUT_BRIDGE
FAIL header_type:162: pci_header_layout(0x82) == PCI_HDR_LAYOUT_CARDBUS
FAIL names:198: !strcmp(pci_class_name(0x06, 0x01), "Bridge/ISA")
FAIL names:199: !strcmp(pci_class_name(0x06, 0x04), "Bridge/PCI")
FAIL names:203: !strcmp(pci_class_name(0x02, 0x80), "Network")
FAIL names:204: !strcmp(pci_class_name(0x06, 0x80), "Bridge")
FAIL story_82557:283: pin == 1
SUMMARY 3/9 PASS
```

RED が指した 5 つの間違いは、どれも **実機でしか表に出ない**:

| # | 素朴な実装 | 実機で何が起きるか |
|---|---|---|
| 1 | `bus << 16 \| dev << 11 \| fn << 8` をマスクせずに詰める | 範囲外の値が隣の欄へ溢れ、**別のデバイスの config を読む**。列挙の上限を書き間違えた日に、警告も例外も無く嘘の一覧が出る |
| 2 | BAR の生値 `1` を「未実装」に畳む | `0x00000001` は **I/O BAR はあるが番地が 0** = BIOS が割り当てていない。票 §5-2 R1 が実機で見たいのはまさにこれで、畳むと事実が消える |
| 3 | I/O BAR の番地も `~0xF` で切る | `0xE808` が `0xE800` に化ける。8255x の I/O 窓は 32 バイト境界に置かれ得るので、L-B が**存在しない番地**を叩く |
| 4 | bit3 を無条件に prefetchable と読む | I/O BAR では bit3 は番地の一部。`0xE808` が「prefetchable な I/O」という存在しないものになる |
| 5 | Header Type の bit7 を落とさずに比較 | マルチファンクションのブリッヂ (`0x81`) が Type 0 扱いになり、**secondary bus を読まずに配下のバスを丸ごと見落とす** |

`story_82557:283` は試験側の誤り (期待値に Interrupt Pin を `0Bh` と書いていた。
SDM 4.1.16 は **常に `01h` = INTA#**)。ハーネスを直した。
`pci_cfg_addr(0, 11, 0, 0x3C)` の期待値も `0x80005B3C` と書き間違えていた
(11 << 11 = `0x5800` なので `0x8000583C`)。**資料を引き直して直したのは期待値の側**で、
実装を期待値に合わせてはいない。

直した後:

```
HOST GNU89 -Werror compile PASS (real drivers/pci_decode.c)
TARGET i386-elf GNU89 -Werror PASS
PASS cfg_addr / probe_values / bar_kind / bar_base / header_type
PASS extract / names / story_82557 / absent
SUMMARY 9/9 PASS
```

`--target` はカーネルと同じ `i386-elf-gcc -Werror` で `drivers/pci.c` ごと通す。
列挙は I/O を触るのでホストでは**回せない**が、型とプロトタイプのずれは手元で捕まる。

## 3. 変異 (否定側) — 7 本すべて RED

```
MUTATION 1 RED (1 件): bus/dev/fn をマスクせずに詰める (範囲外が隣の欄へ溢れ、別のデバイスを読む)
MUTATION 2 RED (1 件): 番地未割り当ての I/O BAR (生値 1) を「無い」に畳む (票 R1 が消える)
MUTATION 3 RED (1 件): I/O BAR の番地を ~0xF で切る (0xE808 が 0xE800 に化ける)
MUTATION 4 RED (1 件): I/O BAR でも bit3 を prefetchable と読む (番地の一部を属性と取り違える)
MUTATION 5 RED (1 件): Header Type の bit7 を落とさない (マルチファンクションのブリッヂを見落とす)
MUTATION 6 RED (1 件): 16 ビットの切り出しをバイト境界で行う (奇数オフセットで値がずれる)
MUTATION 7 RED (1 件): ブリッヂのサブクラスを見ない (Host / ISA / PCI が全部同じ名前になる)
```

変異 1〜5 は §2 の表と同じ「実機だけで表に出る」5 つを、実装を 1 か所ずつ戻して
再現したもの。6 と 7 は表示と切り出しの取り違え。

## 4. 期待値の出どころ

| 何 | どこ |
|---|---|
| `0CF8h` のビット定義 (CONE / bus / dev / fn / reg、**DWORD アクセス必須**) | `docs/hw/undocumented/io_pci.md` 446〜462 行 |
| Type 0 のアドレス語の図 | 同 76〜109 行 (図2)。Type 1 は 111〜141 行 (図3) |
| PC-98 のデバイス番号 (PCMC=0 / C バスブリッヂ=1 / ローカルバスブリッヂ=2 / スロット #0〜2=8〜10) | 同 55〜58 行 |
| PIRQ0〜3 → 8259 は C バスブリッヂの config で PnP BIOS が設定 → **Interrupt Line を読めば IRQ が分かる** | 同 313〜320 行 |
| Type 0 ヘッダのオフセット (vendor 0x00 / device 0x02 / command 0x04 / rev+progif+sub+class 0x08 / header 0x0E / BAR 0x10〜0x24 / int line 0x3C / int pin 0x3D) | `docs/hw/intel/8255x_open_source_sdm.txt` 697〜713 行 (Table 1)。PCI 規格と同じ並び |
| 82557 は `8086:1229`、Class Code `200000h`、Interrupt Pin は常に `01h` | 同 727〜736 行 (4.1.1 / 4.1.2)、1057〜1070 行 (4.1.15 / 4.1.16) |

## 5. ここで試験していないこと (実機で答え合わせする)

- **`0CF8h` の読み戻しが実機で本当に一致するか。** 2 本の値
  (`0x80000000` と `0x80FFFFFC`) で探る。B は bus/dev/fn/reg の書き込める
  全ビットを動かすので、`0CF8h` が素通しのラッチである機械
  (io_pci.md 458 行: bit31=0 のときマーキュリー互換) を弾ける。
  **弾きすぎて実機で「PCI 無し」と出たら、A だけに緩めること** — 判断の分かれ目はここ。
- 82557 が実際にどのデバイス番号に載っているか (オンボードなのでスロット 8〜10 の外のはず)。
- I/O BAR に BIOS が番地を割り当てているか (票 R1)。`0` なら L-B に割り当てが要る。
- ブリッヂ配下のバスが本当に深さ 2 で足りるか。

## 6. ハングしないことの担保 (実機で試す前に決めてある)

`pci_init()` が止まらない理由は 4 つ。実機に持って行く前に構造で決めておく
(実機で固まると電源を切るしかなく、原因も残らない)。

1. メカニズム #1 の読み戻しが一致しなければ **その場で** 戻る。走査へ進まない。
2. 走査は `dev < 32` × `fn < 8` の固定二重ループ。中で範囲は伸びない。
3. ブリッヂへ潜るのは `depth < PCI_MAX_DEPTH` (= 2) のときだけ。
4. 潜る先は **secondary バス番号が現在のバスより大きいとき** だけ。BIOS が
   設定を終えていない機械で secondary に 0 やゴミが入っていても、同じバスを
   走査し直さない。
