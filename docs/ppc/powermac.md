# PowerMac のハードウェア — QuickSilver 調査の基礎

> 上位: docs/ppc/README.md

この文書は macppc の BSD ドライバから確認した装置の発見方法をまとめる。
[未] QuickSilver 個体の OF tree、基板改版、装置番号と外部への配線。
Apple の QuickSilver Developer Note が未入手なので、一般の G4 対応コードを
そのまま QuickSilver 固有の仕様とはしない。

## ホストブリッジと macio

NetBSD は `uni-north` 等の compatible に照合する
(N/sys/arch/macppc/pci/uninorth.c:73)。
UniNorth の reg、bus-range、ranges を読み、PCI の I/O 窓とメモリ窓を作る
(同 :110、同 :121、同 :145)。
このドライバでは PCI bus_space の属性が little-endian。
CPU の big-endian と PCI のレジスタ順序は別に扱う。

macio のドライバは Apple の KeyLargo を含む PCI ID に照合する
(N/sys/arch/macppc/dev/obio.c:140)。
PCI 装置に対応する OF ノードを探し、assigned-addresses の基底と子の reg を扱う
(同 :192、同 :219、同 :294)。
機種固有の番地を定数として写すより、これらの属性の意味を確認する必要がある。

| 装置 | BSD の発見・設定の根拠 |
|---|---|
| OpenPIC / MPIC | macio 子の compatible に照合し、assigned-addresses の基底 + 子の reg を PIC 番地にする。N/sys/arch/macppc/macppc/interrupts.c:73、同 :91 |
| ESCC | `escc` 名に照合し、macio 基底と reg を使い、ch-a の割込みを読む。N/sys/arch/macppc/dev/zs.c:203、同 :239 |
| ATA | ata/ide 名や `keylargo-ata` compatible に照合。N/sys/arch/macppc/dev/wdc_obio.c:91 |
| PMU と ADB | `via-pmu` に照合し、adb 子にアクセス口を渡す。N/sys/arch/macppc/dev/pmu.c:252、同 :358 |
| USB 入力 | OF コンソール側には USB keyboard の ihandle を取得する経路がある。N/sys/arch/powerpc/oea/ofw_consinit.c:316 |

[未] Cuda/PMU の QuickSilver への適用区分、USB ホストコントローラと入力の配線、
ESCC の実配線・クロック・電源。PMU 内部の ADB 経路が存在しても外部 ADB 端子の証拠ではない。

## 割込みと初期化の順序

NetBSD の割込み初期化は PIC 共通層 → 対応コントローラの探索と設定 →
外部割込みハンドラの導入という順
(N/sys/arch/macppc/macppc/interrupts.c:138、同 :168)。
OpenPIC の共通処理は IRQ をマスクし、宛先や spurious vector を設定する
(N/sys/arch/powerpc/pic/pic_openpic.c:99)。
装置 IRQ と CPU 例外オフセット 0x500 は違う番号体系である。

mainbus は CPU 列挙後に `pic_finish_setup()` を呼び、OF バスとルート直下を列挙する
(N/sys/arch/macppc/macppc/mainbus.c:89、同 :116、同 :126)。
UniNorth は PCI 窓を準備し、macio の obio は子を OF の peer 順に config_found する
(N/sys/arch/macppc/pci/uninorth.c:145、N/sys/arch/macppc/dev/obio.c:275)。
装置全体の attach 順が全機種で固定だとはしない。

macio 子の割込みはまず `AAPL,interrupts`、無ければ `interrupts` を読む
(N/sys/arch/macppc/dev/obio.c:296)。
PCI は `interrupt-map` とセル数を読み、親 PIC を引く
(N/sys/arch/powerpc/pci/pci_machdep_ofw.c:221)。
PIC ノードの `interrupt-parent` を調べる処理もある (同 :118)。
[未] 一般 binding の interrupt-map-mask、複数段の親、極性・edge/level の完全な解釈。
QuickSilver の IRQ 一覧は実機 tree と照合して作り、QEMU の番号で埋めない。

## OF の画面

stdout の ihandle を package に変換して画面ノードを識別する
(N/sys/arch/powerpc/powerpc/ofw_machdep.c:174)。
NetBSD の rascons は width/height/linebytes/depth/address を読み、無ければ
一部を OF の interpret 等で補う
(N/sys/arch/powerpc/oea/ofw_rascons.c:234)。
linebytes は行のバイト数で、width × depth / 8 と常に等しいとはしない。
`ofb` の表示領域サイズは stride × height をページに丸める
(N/sys/arch/macppc/dev/ofb.c:202)。
[未] QuickSilver の装着 GPU ごとの物理/仮想番地の対応、色順序、パレット、
MMU 切替後の FB 継続利用。OF の address を無条件に物理番地とみなさない。
