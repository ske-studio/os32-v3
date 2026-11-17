# USB の土台 (PCI の USB カード NEC uPD720101)

> 状態: **実装中 (2026-11-13)** — 設計は確定 ([USB_DESIGN](../../decisions/usb/USB_DESIGN.md))。段 1 の共通の部品と PowerMac の殻は os32powerpc が 1A から着手、PC-98 の側 (1D の入力の側と 1E) は本線。

## 0. ユーザー決定 (原文)
- 2026-11-13 「USB の段 1 を os32powerpc に任せてよいですか？」→「任せる (推奨)」(本線のユーザー)、PowerPC の側でも「受けて良い」 — 段 1 の共通の部品 (drivers/usb/) は PowerPC の PM が QEMU mac99 の内蔵 OHCI で先に通し、PC-98 の結線と Ra266 の実機の受入は本線。
- カード: 「uPD720101これのはず」 (EHCI 1 + OHCI 2、PCI 信号は 5V 耐性 — Renesas のデータシート S16265EJ5V0DS)。
- 2026-11-09 「たまたまですが、quicksliverに同じチップのPCI USBカードが刺さってました。」 — PowerMac G4 QuickSilver にも uPD720101 の PCI カードがある。同じチップを Ra266 (little-endian) と G4 (big-endian) の両方の実機で確かめられる (G4 側は PowerPC が起動して LAN のログが取れる段の後)。
- U2 (Ra266 の PCI の空き): 「その段で入れ替える」 — 段 1 の実機の受入の前に、HighPoint を抜いて USB カードに入れ替える (物理操作はユーザー)。一度「GeForce を外しても良い」となったが「いや、highpointを抜きましょう。別にこれは後回しでいいので」で HighPoint に戻した (2026-11-09)。USB の実装と実機の受入は後回し。
- 設計の往復: 「私が納得するまでラリーは進行してください。ただし、毎ラリー毎に私が論点チェックします」、「USBはBSDの手本を参考に出して良いです」、「シングルタスクの前提を忘れずに。」、「基本的に重くなければシングルタスクの前提で進めてください。重いものは判断します。」、確定版に「OK」。

## 1. 要約
- 手本は FreeBSD の USB (ohci.c・usb_hub.c・usb_device.c・usb_transfer.c・ukbd.c・ums.c)。手順の順を file:line で写し、スレッド・ロック・sleep は持ち込まず、続きは tick と前景の service の周で進める。手本から変えた所は確定設計の D1〜D10。
- HC の故障は 1 本の入口で、止める → DMA と通常のメモリを再起動まで隔離 → 配下は一括で「無い」 → 再起動まで使わない。
- 段: 1 = OHCI・列挙・ハブ・HID (boot のキーボード・マウス) を入力へ、2 = EHCI と companion、3 = 大容量 (読みを先に受入)。ゲームパッド (XInput) はサウンド・入力の HAL の段で、USB の段の後。

## 2. 段の状態
| 段 | 状態 |
|---|---|
| 段 1: 1A (型と契約)・1B (OHCI)・1C (中核)・1D の HID の class・1P (PowerMac の殻と QEMU の受入) | os32powerpc、1A から着手 (2026-11-13)。共通の場所に入れる前に本線へ知らせる。memmap.h・kernel/irq/tick.h は触らない |
| 段 1: 1D の drivers/input/ の PC-98 の側・1E (常駐の部品・platform/pc98 の殻・pci_mmio・memmap.h・tick の枠・KAPI usb_stat_get・Ra266 の実機) | 本線、PowerPC の 1A〜1D の後 |
| 段 2・3 | 未着手 (確定設計 §10・§11)、担当は段 1 の結果を見て決める |

## 3. 実機の列挙 (Ra266、2026-11-09〜10、事実だけ)
- カードは 3 機能: fn0・fn1 = 1033:0035 (OHCI)、fn2 = 1033:00e0 (EHCI)。BAR0 だけを使い、OHCI は 4KB・EHCI は 256B (どちらも mem32・非 prefetch)、BAR1〜5 は 0 (11-09 に HW-BASIC で全部 1 を書いて測った)。
- ある起動では BIOS が fn1 に番地を割り当てず、BAR0 = 0xFFFFF000 (測った値のまま)・interrupt line 0 で渡した。別の起動では番地と irq を持つ。
- スロット 0:12 と 0:13 のどちらでも列挙された。各機能の IRQ と共有は票 TASK_PCI_IRQ_ROUTE §2。ドライバが無いので列挙だけ。
