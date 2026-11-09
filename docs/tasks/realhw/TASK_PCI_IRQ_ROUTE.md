# PCI の IRQ を機器の種類で振り直す (共通の層と PC-98・PowerMac G4 の backend)

> 状態: **設計中 (2026-11-09)** — 設計は確定、実装を待つ ([PCI_IRQ_ROUTE_DESIGN](../../decisions/realhw/PCI_IRQ_ROUTE_DESIGN.md))。前提は GeForce の位置の直し (票 TASK_GF_BDF)。PM は設計に専念中のため実装の着手はユーザーの解除の後。

## 0. ユーザー決定 (原文)
- 2026-11-09 (実機ノート経由):「デバイスの種類で IRQ を振り直す実装があったほうが良い」
- 2026-11-09「振り直す動作の基準はあくまで現代の人間が迷わない割り振りで、このような実装の曖昧さを現代基準で抽象化します」
- 2026-11-09「実装検証段階では色々なデバイスで組み合わせを試しましょう」
- 2026-11-09「これは、PowerPCでも効いてくる部分だと思っているのですが」

## 1. 詳細設計 (確定) の要約
全文は [PCI_IRQ_ROUTE_DESIGN](../../decisions/realhw/PCI_IRQ_ROUTE_DESIGN.md)。
- 根拠: Ra266 の 0:6.0 (NEC 1033:002c) の 0x60〜0x63 = PIRQ0〜3 → IRQ の表 (bit7 = 繋がない)。資料は Xa の 1033:0001 なので、書いた値が効くかは実機で確かめる。
- 層: 共通 (機器ごとの登録 `pci_irq_settle`・`pci_irq_attach`/`detach`・割り当ての一覧・種類ごとの方針) / backend (PC-98 = 0:6.0 の表と 8259、G4 = 起動の早い段で OF の木から表を作り MPIC を操作、読むだけ)。
- 段 1〜10 (共通・PC-98・lspci・82557・GeForce・dispatch の計数) は本線、段 P (G4 の backend) は os32powerpc。
- PM 決定: 候補の線は BIOS が使った {3,5,6} から / 挿し場所は決めず構成の表で受入 / 止め口は起動設定 `pci_irq_route=0` / G4 は初版は同じ優先度。
- 受入は構成の表 (HighPoint+GeForce・USB+GeForce の 2 通りの並び・USB と HighPoint の差し替え) を実機で。抜き差しはユーザー、採取は実機ノート。
