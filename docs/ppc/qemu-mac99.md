# QEMU mac99 と実機 — 資料で確認できる範囲

> 上位: docs/ppc/README.md

QEMU のソースは参照しない。以下はローカルの QEMU 8.2.2
(Debian `1:8.2.2+ds-0ubuntu1.18`) の help を採取した結果。
採取時の作業日は 2026-11-08、資料木の管理日付は依頼に合わせ 2026-11-09。
指定された `/usr/share/doc/qemu-system-ppc/system/` 配下には通常ファイルが無く、
`ppc/` も空だった。配布文書に基づく mac99 の構成確認は [未]。

## 観測したコマンドと出力

| コマンド | 出力で確認した項目 |
|---|---|
| `qemu-system-ppc -M help` | `mac99` が Mac99 based PowerMAC として列挙される |
| `qemu-system-ppc -cpu help` | `7450` は 7450_v2.1、PVR 80000201。`7455` は 7455_v3.2、PVR 80010302 の alias |
| `qemu-system-ppc -M mac99 -device help` | macio-newworld/oldworld、ADB keyboard/mouse、USB keyboard/mouse、VGA 等が列挙される |

生の出力は `/home/hight/os32-tmp/run/ppc-docs/` の
`qemu-machines.r2.txt:6`、`qemu-cpus.r2.txt:209`・同 :222、
`qemu-devices.r2.txt:5`・同 :125・同 :139・同 :179。
これらのコマンドはゲストを起動しない。

## 比較してよい範囲

| 項目 | help から分かること | 実機への適用と残る確認 |
|---|---|---|
| CPU | 7450/7455 のモデル指定候補がある | QuickSilver 個体の PVR・revision は [未]。命令とキャッシュの完全な一致は示さない |
| macio | PCI 装置名 macio-newworld が存在する | KeyLargo の機能・レジスタの実装範囲と標準構成は [未] |
| PIC | この help には openpic 名の列挙が無い | PIC が無いとは判断できない。mac99 内蔵 PIC の種類・配線は [未] |
| ESCC | この help には escc 名の列挙が無い | 内蔵の有無・番地・送受信の挙動は [未] |
| Cuda / PMU | この help には cuda/pmu 名の列挙が無い | 選択方法・標準構成・ADB との関係は [未] |
| 入力 | ADB/USB の装置名がある | mac99 の既定入力と QuickSilver の USB 配線は [未] |
| 画面 | VGA/cirrus-vga の PCI 装置名がある | mac99 の既定 GPU と実機搭載 GPU、OF の画面プロパティの一致は [未] |

`-device help` は利用可能な装置型の一覧であり、mac99 の実際の装置ツリーや
各型が接続できることの保証ではない。既定の CPU 型も `-cpu help` だけでは決まらない。
QEMU で起動できた事実から、実機の clock/power・cache・DMA・電気配線の正しさを
証明することはできない。

[未] QEMU の同版の system/ppc/powermac 文書と一般 PowerPC 機種文書を
取り寄せ、mac99 の via 選択・PIC・標準装置を資料だけで補う。
実機との差の確定には Apple QuickSilver Developer Note と対象個体の OF tree が必要。
