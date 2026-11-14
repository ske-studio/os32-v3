# PowerPC の USB 入力の現物（2026-11-14）

- 調査版: `ec2fa85e`。以下はコード上の経路であり、今回の実機再試験の結果ではない。
- ユーザー報告の PPC 入力成功を前提に、鍵盤の文字入力とマウスのログ到達を分けて読む。マウスの入力配送はログだけ（`platform/powermac/usb_platform.c:167`）。

## 経路

| 段 | 現在の処理と根拠（file:line） |
|---|---|
| ホスト探索 | OF の UniNorth PCI root/ranges を保存し PCI class 0x0c0310（OHCI）の BAR0 を写す。mac99 内蔵・G4 の KeyLargo も PCI 経由の OHCI。PCI カード専用の選択ではなく、最初に active になった root で探索を止める（`platform/powermac/usb_tree.c:38`、`platform/powermac/usb_boot.c:63`、`platform/powermac/usb_boot.c:77`、`docs/decisions/usb/USB_DESIGN.md:817`）。 |
| ホスト実行 | `ohci_ops` で起動。USB IRQ は接続せず、100Hz slot と前景の 10ms polling から service。PPC boot は EHCI を選ばず、共通の `ehci_quiesce` は別部品（`platform/powermac/usb_boot.c:86`、`platform/powermac/usb_boot.c:103`、`platform/powermac/usb_boot.c:107`、`platform/powermac/usb_platform.c:129`、`drivers/usb/ehci_quiesce.c:14`）。 |
| 列挙・ハブ | SET_ADDRESS → config 記述子 → SET_CONFIGURATION → class 選択。ハブの割込み IN と探索を service の周で進める（`drivers/usb/usb_enum.c:92`、`drivers/usb/usb_enum.c:133`、`drivers/usb/usb_enum.c:151`、`drivers/usb/usb_enum.c:176`、`drivers/usb/usb_hub.c:101`、`drivers/usb/usb_service.c:80`）。 |
| HID | boot subclass/protocol で選択、SET_PROTOCOL の値 0、鍵盤は SET_IDLE も行う。report descriptor 解析なし。鍵盤 8B、マウス 3B（wheel/report ID なし）（`drivers/usb/hid_boot.h:22`、`drivers/usb/hid_boot.h:46`、`drivers/usb/hid_boot.h:57`、`drivers/usb/hid_mouse.c:2`）。 |
| キーイベント | usage → PC-98 共通 sc、修飾を先に押下差分通知。500ms 後に 30ms 周期で repeat → `usbp_input_key/repeat`（`drivers/usb/hid_kbd.c:18`、`drivers/usb/hid_kbd.c:26`、`drivers/usb/hid_kbd.c:29`、`drivers/usb/hid_kbd.c:32`、`drivers/usb/hid_kbd.c:69`、`drivers/usb/hid_kbd_map.c:8`）。 |
| マウスイベント | byte1/2 を符号付き dx/dy、byte0 下位3bit を buttons として通知するが、PowerMac 側はログだけ。PointerOps/GUI への接続はここにない（`drivers/usb/hid_mouse.c:10`、`platform/powermac/usb_platform.c:167`）。 |
| 今の入力口 | キーは `pm_input_usb_key` → src ごとの押下表 → JIS 文字化 → キュー → `pm_input_usb_take`。LAN と交互に取り、`InputOps` として登録する（`platform/powermac/usb_platform.c:160`、`platform/powermac/input_usb.c:23`、`platform/powermac/input_usb.c:30`、`platform/powermac/input_usb.c:51`、`platform/powermac/input_boot.c:34`、`platform/powermac/input_boot.c:71`）。 |
| PLN1・FB との関係 | PLN1 TERM_IN は別の byte キューから decoder を通り同じ InputOps に合流。USB を PLN1 に注入する構造ではない。FB は表示 backend（`platform/powermac/lan_terminal.c:49`、`platform/powermac/input_boot.c:21`、`platform/powermac/input_boot.c:23`、`platform/powermac/console.c:19`）。 |
| シェル・GUI | USB キーまたは LAN 接続で `/bin/sh.bin` を起動、sh は KAPI の kbd_getkey を読む。PPC provider の GUI mode は空、raw=-1・pressed/modifiers=0。GUI 用の完全な状態入力が使えるとはいえない（`platform/powermac/shell_boot.c:33`、`platform/powermac/shell_boot.c:36`、`userland/shell/ui.c:23`、`platform/powermac/input_boot.c:49`、`platform/powermac/input_boot.c:53`、`platform/powermac/input_boot.c:63`）。 |

## 依存の分類

| 部分 | PowerPC / PowerMac に依るもの | 共通にできるもの・根拠 |
|---|---|---|
| DMA 番地・キャッシュ | pm_v2p_ram、先頭16MB の bus 窓、dcbf→sync→cache-inhibited 写像、sync/eieio | bus/va を分けた DMA 契約と塊の分割。`platform/powermac/usb_platform.c:33`、`platform/powermac/usb_platform.c:37`、`platform/powermac/usb_platform.c:43`、`platform/powermac/usb_platform.c:65`、`drivers/usb/usb_dmamem.c:8`、`drivers/usb/usb_dmamem.c:24` |
| PCI・MMIO | OF ranges から PCI→CPU 番地、BAR の写像・config backend | OHCI レジスタは LE helper 越し。BE CPU でも同じ HC 処理を使う。`platform/powermac/usb_platform.c:74`、`platform/powermac/usb_platform.c:101`、`platform/powermac/usb_platform.c:105`、`drivers/usb/ohci_internal.h:10`、`drivers/usb/ohci_internal.h:14` |
| エンディアン | CPU 固有の変換は helper 側 | DMA 記述子は dma_le32_put/get、USB 記述子も LE。`drivers/usb/ohci_sched.c:82`、`drivers/usb/hid_boot.h:49` |
| 割込み・時計 | IRQ attach は -1、PPC tick と EE 制御、timebase を ms に換算 | service が intr_collect と callback を回す形、締切比較と repeat の規則。`platform/powermac/usb_platform.c:129`、`platform/powermac/usb_platform.c:132`、`platform/powermac/usb_platform.c:144`、`platform/powermac/usb_boot.c:109`、`drivers/usb/usb_service.c:61`、`drivers/usb/usb_service.c:75`、`drivers/usb/hid_kbd.c:69` |
| HID・キー表 | CPU 命令への依存なし。ただし sc の体系と JIS 配列は OS32 の方針 | boot report の差分・rollover 除外・repeat・usage→sc と cooked map は抽出可能。`drivers/usb/hid_kbd.c:15`、`drivers/usb/hid_kbd.c:29`、`drivers/usb/hid_kbd.c:69`、`drivers/usb/hid_kbd_map.c:8`、`platform/powermac/input_keymap.c:27` |
| キュー・入力能力 | irq_save/restore の実体と待機中の進行は platform 依存 | src 別押下・文字キューは CPU 非依存の論理。現 InputOps は能力不足を固定値で隠すため AC-INPUT-1 の F6/F7 が必要。`platform/powermac/input_usb.c:30`、`platform/powermac/input_usb.c:53`、`platform/powermac/input_boot.c:47`、`docs/decisions/ARCH_COMPAT_CONTRACT.md:103`、`docs/decisions/ARCH_COMPAT_CONTRACT.md:146`、`docs/decisions/ARCH_COMPAT_CONTRACT.md:147` |

## PC-9821 の PCI USB カードへ

- 票は NEC uPD720101（OHCI×2 + EHCI×1）の段1を確定設計へ委譲する。G4 にも同チップのカードがあるが、今回そのカードの実機成功を再確認したものではない（`docs/tasks/realhw/TASK_USB.md:3`、`docs/tasks/realhw/TASK_USB.md:8`、`docs/decisions/usb/USB_DESIGN.md:2`）。
- そのまま使う候補: 共通 OHCI・DMA 内部配置・転送寿命・列挙/ハブ・boot HID と usage→sc。platform の usbp_* を PC-98 実装に置換する契約であり、PowerMac の OF・timebase・DMA 写像は持ち込まない（`drivers/usb/usb_dmamem.c:24`、`drivers/usb/usb_xfer.c:49`、`drivers/usb/usb_service.c:61`、`docs/decisions/usb/USB_DESIGN.md:645`、`docs/decisions/usb/USB_DESIGN.md:823`）。
- 足りない結線（計画）: 本体鍵盤と USB の src を合成する kbd_merge、mouse_ext、PC-98 の platform 殻・常駐/部品・PCI MMIO 予約・tick・usb_stat_get。担当は本線 1D の入力側と 1E（`docs/tasks/realhw/TASK_USB.md:21`、`docs/decisions/usb/USB_DESIGN.md:566`、`docs/decisions/usb/USB_DESIGN.md:570`、`docs/decisions/usb/USB_DESIGN.md:724`、`docs/decisions/usb/USB_DESIGN.md:725`、`docs/decisions/usb/USB_DESIGN.md:727`）。
- 段1でも BIOS の EHCI 引継ぎ・静止と CONFIGFLAG=0 が必要。段2の EHCI 通信とは別。Ra266 のカード入替と実機受入は未了の関門で、PPC 入力成功だけでは代替しない（`docs/decisions/usb/USB_DESIGN.md:334`、`docs/decisions/usb/USB_DESIGN.md:335`、`docs/decisions/usb/USB_DESIGN.md:813`、`docs/tasks/realhw/TASK_USB.md:9`）。

## 入力 HAL の境目（提案）

| 候補 | 評価と根拠 |
|---|---|
| **推奨: HID 解釈後、文字化・src 合成前のイベント** | usbp_input_key/mouse/release を共通入力層へ渡す。sc/down/src と相対移動・ボタンを保持し、cooked InputOps と in2 の両方を作る。既定設計も merge 前に src 別採取し、repeat は in2 へ入れない（`docs/decisions/sound/AV_HAL_DESIGN.md:420`、`docs/decisions/sound/AV_HAL_DESIGN.md:421`、`docs/decisions/sound/AV_HAL_DESIGN.md:423`、`platform/powermac/input_usb.c:23`）。 |
| 文字化後の InputOps | sh の共通入口としては使えるが、押下/解放・装置別状態を復元できず Raw Input の境目には不足。既存口を残す互換出口とする（`platform/powermac/input_boot.c:58`、`platform/powermac/input_boot.c:62`、`docs/decisions/sound/AV_HAL_DESIGN.md:356`）。 |

- XInput 形はパッドの上位状態であり、鍵盤/マウスの boot report と同一にしない。一般 HID report 解析と padmap は後段。入力不在と未押下を分離し、待機中の進行条件も HAL の能力に含める（`docs/decisions/sound/AV_HAL_DESIGN.md:339`、`docs/decisions/sound/AV_HAL_DESIGN.md:433`、`docs/decisions/sound/AV_HAL_DESIGN.md:449`、`docs/decisions/ARCH_COMPAT_CONTRACT.md:103`）。
- kbd_inject は UTF-8 を読者宛キューへ積む別経路で、USB の物理イベント境界にはしない（`kernel/kbd_inject.c:186`、`kernel/kbd_inject.c:210`、`kernel/kbd_inject.c:248`、`platform/powermac/usb_platform.c:160`）。
