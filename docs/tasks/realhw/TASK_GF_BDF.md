# GeForce の PCI の位置を起動時の列挙で見つける (決め打ち 0:13.0 の撤去)

> 状態: **実装中 (2026-11-10)** — 設計は確定 ([GF_BDF_DESIGN](../../decisions/realhw/GF_BDF_DESIGN.md))。段 A・B は main に着地 (c961d1cf、USB の BAR の直し 94f458ea の上)。段 C (文書の「0:13.0」の書き換え) は 2026-11-10 に済。残りは実機の受入 (構成の表の R1) と、その後に PM がする `RA266.md` §1-1 の更新・`gf_accept.c:177-178` の直値 0。

## 0. ユーザー決定 (原文)
- 2026-11-09 実機で USB カードと GeForce のスロットを入れ替えたら GeForce が 0:12.0 になり起動の検査で降りた。選択:「列挙で探す (推奨)」
- 2026-11-09「実装検証段階では色々なデバイスで組み合わせを試しましょう」「これは、PowerPCでも効いてくる部分だと思っているのですが」

## 1. 詳細設計 (確定) の要約
全文は [GF_BDF_DESIGN](../../decisions/realhw/GF_BDF_DESIGN.md)。
- 探索は CPU に依らない `drivers/pci/pci_locate` (単一バスの fn0 の探索)、GF 側は `recover_gf_dev()`。4 像 (カーネル・geforce.mod・kdebug.bin・hwdev.mod) が同じ起動中の同じ位置を使う。
- 試験の fixture: GPU=13 (今の実測)、GPU=12+USB=13 (本番の書き込みの宛先がすべて 0:12.0)、GPU 無し (PCI の書き込み 0 回)。
- 同じ枝に PM が足す: USB カードの BAR の表 (wt/gf-usbbar)、`hwdev/gf/irq.c` の `GF_IRQ_LINE 6U` を見つけた BDF の 0x3C の線に。
- 実機の受入は今の 0:13.0 の構成を必須、0:12.0 はユーザーがスロットを入れ替えたときの構成の表の 1 行。
- 設計との差 (2026-11-10 の照らし直し): B3 は `tools/tests/pci_locate_host.c` 1 本が `recover_gf_locate.c` も組む (`recover_gf_locate_host.c` は無い)。B4 は名前の付いた表を作らず `gfinit_bus_host.c:417` で `GF_T_DEV=12` に切り替える。
