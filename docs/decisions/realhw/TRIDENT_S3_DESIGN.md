# TRIDENT_S3_DESIGN — Trident ProVidia 9682 の表示ドライバ

状態: **現行 (2026-11-07)**。段の状態は票 TASK_TRIDENT_DRIVER が持つ。
実機は PC-9821 Ra266、非 primary の ProVidia 9682、640×480×8、pitch=640。
実行モデルはシングルタスク・協調切替。設計の前提は [TRIDENT_V6_DESIGN](TRIDENT_V6_DESIGN.md)。

## 1. 出典と対象の枝

- D: MIT/X11 `trident_driver-1.4.0.c`、PC98 対応の版。
- X: MIT/X11 `xf86-video-trident/src/` の `trident_dac.c`、`trident_pll.c`、`trident_video.c`、`trident_shadow.c`。
- V: `xorg-vgahw/vgaHW.c`。著作権・許諾全文は `drivers/video/trident/trident.h` と移植したファイルに保持する。
- メーカー資料: NEC LCD172VXM/192VXM 取扱説明書 p.20 の同期条件。9682 レジスタ仕様書は未入手。未入手部分を推測した手順で埋めない。

9682 は非 Cyber、非 3D、旧 PLL。25.175MHz、8bpp、開始位置0、非 interlace、非 MUX、PCI/MMIO/linear の枝を採る。
LCD、3D、追加 gamma、TV、primary の font/text 保存、MCLK 保存オプションは通らない。
PC98 Init の MCLK 80MHz 設定は保存オプションとは独立して毎回行う。

## 2. 状態と寿命

| 状態 | 寿命・切替 |
|---|---|
| boot 資源 | core が DEVICE 台帳予約、MMIO/LFB 写像、DISPLAY/CLIENT 面を用意。CLIENT は RAM 300KiB |
| pin/arm | module loader が pin、indexed I/O の前に復帰口を arm。Close 後も常駐 |
| preinit_done | 識別と SPR 読取の成功時に設定。以後の ScreenInit でこの2処理だけ省略 |
| saved | VGA/Trident は毎 ScreenInit 保存。DAC は V の cmapSaved と同じ初回 cache |
| init_done | PC98 Init の成功を表す。毎 ScreenInit で更新 |
| active | D の vtSema。ModeInit の CRTC unlock 後に true、Leave/Close 完了で false。V:580 の Protect 条件にも使用 |
| screen_ready | ScreenInit 成功後の EnterVT 利用可否。ハード設定を省略する条件には使わない |
| reference | X/pll の基準発振 cache。同じ機器で PAL/NTSC 選択を維持 |
| last_rc | 直近の試行の失敗。次の ScreenInit/EnterVT はリセットし再試行。恒久失敗 latch ではない |

VGA IOBase は Save 時の MISC、Mode の VGA Restore で color、保存状態の Restore で保存 MISC に切り替える。
SR0E の末尾 XOR02、CR50=00、CR3A=00 を保持する。
復帰中の再帰は拒否し、Restore と Disable の両方を試み、各戻り値を保持する。
HAL の void 境界の失敗は query で返す。core の強制選択では Trident の prepare を先に呼び、前回の失敗を更新してから probe するため、次回 init を妨げない。

## 3. 呼出順

| 入口 | 手順・出典 |
|---|---|
| ScreenInit | PreInit 相当を初回だけ → Save (D:3472) → PC98 Init (D:3479-3481) → ModeInit (D:3486) → SaveScreen blank (D:3490) → AdjustFrame (D:3491) → HAL CLIENT/palette → InitVideo (D:3744-3748) |
| ModeInit | CRTC unlock → vtSema=true (D:1005-1010) → Trident モード画像 → VGA Protect/Restore → TridentRestore → unprotect → PC98 Enable (D:1021-1044) |
| EnterVT | ModeInit/PC98 Enable を毎回実行 (D:1206-1213)。active でも省略しない。Save、PC98 Init、InitVideo は再実行しない |
| LeaveVT | Restore → CRTC lock → PC98 Disable (D:1242-1250) |
| CloseScreen | active のとき PC98 Disable → Restore → CRTC lock。leave 後・二重 close は I/O なし (D:3333-3349) |

PC98 Init は D:616-692 の両 SR0C 枝、MCLK/FIFO を保持。
Enable/Disable は D:699-775 の 68h、6Ah、0FACh、CR23/29、拡張 DAC[4] を保持。
0FACh は共有 route の条件付き書込みに置換せず、Trident の順序で2/0を出力する。

SaveScreen は V:618-635 の SR1 読取 → SR0=1 → SR1 bit5 設定 → SR0=3。OUTB の対で書く。
AdjustFrame は D:1182-1190 の CR0C/0D を OUTW、CR1E bit5 と CR27 bits0..2 を OUTB の RMW で更新。
開始0でも後半の手順を省かない。後の HAL present が blank の解除を暗黙に行う契約はない。

## 4. 容量・転送・資源

SPR の全表は D:2919-2975。申告容量、BAR decode、写像長は別の値として持つ。
転送は SPR 申告容量と LFB 写像・decode、CLIENT の各範囲に収める。
全域 VRAM 書込み検証、逆順 undo、verified_bytes、scratch は持たない。
X/trident_shadow.c:19-36 の行単位 memcpy に合わせ、整列部分は span32、端は byte で転送する。
VRAM の読戻しや、常設診断による SR/CR/AR/DAC selector 操作を present に入れない。

PCI は vendor/device と一般の memory BAR 契約を照合する。固定 BDF は Ra266 の装置を探す入口であり、probe の拒否条件にはしない。
revision、BAR の固定アドレス、BAR2、ROM、隣接全走査を機種固有の起動条件にしない。
取得した BAR の番地を台帳・写像・面へ渡す。RAM 重複・台帳予約失敗・写像失敗は OS の資源契約として返す。
Close で共有写像を解除せず、arm 後の rollback は拒否する。arm 前は成功済みの写像・面・RAM を解放する。

## 5. 検証

Trident の host 試験は libc なしの ILP32 実行形式を qemu-i386 で動かす。
部品ごとの独立した手本の trace を使い、幅・順序・値・RMW の保持ビットを検査する。
ScreenInit の初回/反復、active EnterVT、leave/close、失敗後の再試行、容量全表、present の画像/端/範囲を確認する。
新しい blank/viewport/video/DPMS は各手順の変異をコンパイルし、実行時 RED を確認する。
`make all`、Trident host の変異なし実行、追加・変更試験の `--mutate`、`tools/checks/run.py --lint`、差分の空白検査を行う。
全体の検査・全体の変異は今回の範囲外。実機表示、VSYNC 待ち、復帰品質は host だけでは確認できない。
実機での冷間起動、再入場、98 文字の復帰、10往復、所有者終了/fault/STOP、写真・serial・版 hash の記録は PM の受入に残る。

## 手本どおりに揃えた段で決めたこと (2026-11-07)

- NoAccel=true、SWCursor (HWCursor=false) を採用。D:2065-2073 の明示オプションであり、未入手の TridentAccelInit 本体を独自に補わない。
- X/trident_dac.c:694-700 の NoAccel のゼロ初期化画像に合わせ CR36=00。D:3664-3693 の AccelInit、D:1212-1213 の再初期化、D:1234-1240/3333-3340 の XAA/EXA 同期はこの枝で呼ばれない。
- D:3695-3703 は software cursor 管理のみで HWCursorInit を呼ばない。CR40..4F や Show/Hide を追加せず、モード画像の CR50=00 は保持する。
- ScreenInit 後半に blank と AdjustFrame、末尾に video reset を追加。X/trident_video.c:225-270,350-383,1332-1343 の 9682/8bpp 枝を移す。
- video reset は VSYNC low→high を2回待ち、CR8E=84 → 拡張 DAC[57]=C0 → CRBE=26 → DAC[50]=FF,51=00,52=00,54=FF,55=00,56=00。独自の待ち上限を足さない。色 key は X/driver:1647-1650 の既定値を depth8 で mask した FF。
- DPMS は D:1299-1340 の On/Standby/Suspend/Off を関数として追加。HAL に上位の要求口がないので登録・自動 On 呼出しは足さない。
- 全域 VERIFY と undo、verified_bytes による公開条件、first_init_done、恒久失敗 latch、固定 BDF/BAR/revision と BAR2/ROM/隣接走査の拒否、byte 転送の強制、常設レジスタ診断を除去。
- present の書き方 (2026-11-07、PM 決定): 手本 (X/trident_shadow.c:19-36) は memcpy だが、Ra266 の PC-98 の経路では LFB への 32 ビットの書き込みで格子と詰まった色の帯になる (a59a7c7・45a6ac9a の 2 回、8 ビットでは正しい)。PC-98 の事情として present は 8 ビットで書く (`TRI_PRESENT_USE_SPAN32` 0)。
