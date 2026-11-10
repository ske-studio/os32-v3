# kdebug/gfinit_resources.c — 設計
> 状態: **現行 (2026-11-10)**
ソース: `kdebug/gfinit_resources.c`、`drivers/pci/pci_barsize.c`

BOOT_PCI_ENUM の列挙時に全装置の BAR を一度測り、GeForce の資源除外に使う。
公開 `pci_dev` と KAPI の配置は保ち、長さ・種類・prefetch・未割当状態を核の別表に保存する。
`pci_bar_get(bus,dev,fn,reg,out)` は記録をコピーする私的な kernel/module import。
記録が無い場合に再測定せず、資源収集を失敗させる。

## 測定と復元

手本は FreeBSD releng/14.3 `sys/dev/pci/pci.c:3117-3201` (`pci_read_bar`)、
同 `:3320-3417` (`pci_add_map`) と `pcireg.h` / `pcivar.h`。
仕様は PCI Local Bus 3.0 §6.2.5.1。メーカー資料は
[Intel 8255x SDM rev.1.3 §4.1.10](https://www.intel.com/content/dam/doc/manual/8255x-10-100-mbps-ethernet-controller-software-dev-manual.pdf)。

通常 BAR は元の値と COMMAND を保存し、該当する MEM または IO decode を WORD 書込みで止める。
全ビット 1 を書き、mask を読み、元の BAR、64bit 上位、COMMAND の順に戻す。
64bit は下位・上位の両方を書いてから上位・下位を読む。GeForce も同じ手順。
Bus Master と Status は変更しない。測定1回の保存から復元までを irq_save / irq_restore で囲み、yield しない。

Type 0 は 6 本、Type 1 は 2 本、CardBus は 1 本を測定する。
Type 1 のバス番号や転送窓に sizing 値を書かない。
IDE (class 01.01) の互換モードのチャネルは BAR0〜3 の対応するペアを測らない
(FreeBSD `pci_ata_maps`:3497-3553 の progif bit0 / bit2 判定)。
ROM は Type 0 の 0x30 / Type 1 の 0x38 のうち、起動時に enable=1 のものだけ測る。手本どおり COMMAND は変えず、
0xfffffffe を書いて enable を立てず、元の ROM 値へ戻す。

mask の最下位の実装ビットから長さを求める。0 mask・不正な属性・最小長未満は長さ 0。
base=0、または元の値が mask と等しければ未割当てとして記録する。
BIOS の割当てを保持し、FreeBSD の後段の再割当て・decode の有効化は行わない。
静的表が満杯でも測定と復元は続けるが、保存できない装置の照会は失敗する。
起動ログは実装された BAR ごとに BDF・種類・prefetch・長さ・base を出す。

## GeForce の除外

bus 0 の生 config を走査し、fn0 不在と multifunction に従って function 数を決める。
GeForce 自身を除く MEM 有効装置の memory BAR と、有効な ROM を測定記録で除外する。
未割当ては除外しない。64bit は 2 スロットを消費し、上位も記録と照合する。
生値の変化、測定不能、4 GiB を超える範囲、不整列、件数超過は拒否する。
Type 1 の転送窓の検査は維持し、配下装置の領域を窓全体で除外する。

入口 `gfinit_resources(exclude,count)` は成功時だけ count を確定し、失敗時は 0。
診断時は理由・BDF・reg・raw を表示し、production は reject 構造体に残す。

## 試験

`tools/tests/gfinit_bus_host.c` は mask を返す fake config と実際の測定処理を組む。
USB・SCSI 1000:000f・82557・Trident・GeForce、ROM、64bit、bridge、未割当てと
全 config 復元を照合する。書込み順と probe 中の decode 無効も照合する。`test_gfinit_bus.py --mutate` は decode 復元漏れも実行時 RED にする。
`test_pci_scan.py` は本物の pci_init / pci_scan_bus / pci_record を fake I/O に対して実行し、
再列挙・表の上限超過・function 数・ROM enable・IDE モードを検査する。
