# TASK_INPUT_HAL — 入力の HAL と USB HID の共通化

> 状態: **実装中 (2026-11-15)** — 設計は確定、段 1p・1a・1b は main。詳細設計は [INPUT_HAL_DESIGN](../../decisions/INPUT_HAL_DESIGN.md)。
> 関係: 票 TASK_AV_HAL (入力の in2・XInput)、票 TASK_ARCH_CTX (実行文脈)、PowerPC の P3 (LAN の SH) はこの票の 1p・1a・1b・4c を前提にする。

## 0. ユーザー決定 (2026-11-14〜15)
| 項目 | 決定 (原文) |
|---|---|
| 発端 | 「現在のGUIやCUIはドライバーが直接シェルやGUIに向かって入力している状態ですよね？なるべくHALに向かうようにしていきます」「アーキテクチャの拡張や新規デバイス対応はこのあたりで一度収めてHALを中心にしばらく据えようと思います」 |
| USB | 「特に、PPCですでにUSBでの入力を成功させています。これのアーキテクチャ依存も解消したいです」 |
| U1 | 公開 ABI の切り替えの段だけ 1 段 400 行の例外:「U1認める」 |
| U2 | 入力元の上限:「推奨: 16 枠に広げ、上限は定数 1 か所 (IN2_MAX_DEV) で持つ」→「OK」 |
| U3 | PowerPC の CTRL+C: **確定 (2026-11-15)** — 追補 [INPUT_BIND_DESIGN](../../decisions/INPUT_BIND_DESIGN.md) (BSD キーマップを踏襲した操作の割り当て表、文字は BSD・操作は DOS、Mac は OS X) |

## 1. 詳細設計 (確定) の要約
- 層: 装置 (PC-98 鍵盤・バスマウス・USB の OHCI/HID) → 入力元ごとのイベント (16B) → 共通の入力状態 (入力元ごとの押下の表・合成・焦点・独占) → 出口 (今の InputOps/PointerOps の互換、in2)。PowerPC の usbp_input_key/mouse/release の切り目を採る。
- 文字端末 bridge (kernel/input_legacy.c): serial の混入・GUI への注入・park・待ちの期限を共通に。serial と注入は provider の関数ポインタ。
- 段の表は設計の §7、受入は §8。段 0 は T1b (main) が担う。

## 2. 段の状態
| 段 | 状態 |
|---|---|
| 1p・1a・1b (bridge) | main (b27213d2・73c8664b・d8d529fb) |
| 4c-pre・4c | 実装中 (4c は 4b より先、PowerPC P3 ③b のため。順は 4c → P3 ③b → 4b) |
| 4b-map | 追補 INPUT_BIND_DESIGN §6 の段の後 |
| keymap2 | 追補 2 [INPUT_KEYMAP_DESIGN](../../decisions/INPUT_KEYMAP_DESIGN.md) §7 の段 (os32powerpc: H-usb・H-hid・H-usbp・S1m・S2h・S2u・S2a・S2m・S3m・S3b、本線: そのほか。x86 の USB 鍵盤は本線の 1E で S2a につなぐ) |
| その他 | 未着手 |
