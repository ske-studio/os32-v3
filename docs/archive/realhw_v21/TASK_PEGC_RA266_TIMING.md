# v2.1 PEGC 実機タイミング修正 — PC-9821Ra266

> 状態: **完了記録 (2026-09-29)** — 同じ課題の正典は [TASK_PEGC480_REALHW](../../tasks/realhw/TASK_PEGC480_REALHW.md) (記録本体と受け入れ)。この票は v2.1 の課題の起票として残す。(2026-09-29 の棚卸しで更新)
>
> 発行: 2026-09-28 / それまでの状態: **完了 (画面を見ない条件で受け入れ、2026-09-29)** — 実機の目視確認だけ残る。記録と受け入れは [TASK_PEGC480_REALHW](../../tasks/realhw/TASK_PEGC480_REALHW.md) §3-3・§5、[RELEASE_v2.1](../../RELEASE_v2.1.md) §2-1
> 対象リリース: **kernel v2.1**
> 対象: PC-9821Ra266 実機 / PEGC 640x480x256
> 関連: [GUI設計](../../tasks/gui/DESIGN.md) / [PEGC完了記録](../gui_v11/TASK_H2_pegc.md) / [ドライバ仕様](../../05_drivers.md#5-5-グラフィック-gfx-hal--libos32gfx)

## 0. 位置づけ

v1.1 の PEGC 640x480 backend は NP21/W を主な検証環境として受入済みだが、
PC-9821Ra266 実機で表示タイミングに関する新しい不整合が観測された。

この課題は v3 へ先送りせず、**現行系の実機互換修正として v2.1 で解決する**。
GUI API や Win98 互換層の設計変更を目的にせず、PEGC backend の実機モード設定を
機種の起動時 BIOS 状態に依存しない形へ修正する。

## 1. 実機で観測した症状

ユーザー実機 PC-9821Ra266 で次を観測した。

1. OS32 を通常ブートした直後、接続ディスプレイは入力信号を **31kHz / 720x350 相当**として認識する。
2. `os32gui` をデフォルトの GFX 設定で起動すると PEGC が選ばれると見られる。
3. GUI 起動後、ディスプレイ側の認識は **31kHz / 640x480** に変化する。
4. しかし画面内容は正常な 640x480 PEGC 表示にならず、表示が崩れる。

ディスプレイの「720x350」は入力同期から既知モード名へ分類した表示である可能性があるため、
PC-98 側の論理解像度が本当に 720x350 であるとは現時点では断定しない。

## 2. 現在の実装と疑わしい前提

現行 `gfx/backend_pegc.c` は 640x480 PEGC 初期化時に以下を行う。

- 09A8h を 31.47kHz に設定
- master/slave GDC へ 480 line 用 SYNC を送信
- graphics GDC の SCROLL を設定
- VRAM を 800-line 構成へ変更
- PEGC 256色 packed pixel modeへ変更
- F00000h linear windowを有効化

一方、**GDC PITCH は明示設定せず、起動時 BIOS の状態を引き継ぐ**。
`include/pegc.h` の現行コメントも、NP21/W の BIOS 実装を根拠に
「BIOS既定 40 + GDC clock bit7=0 で実効 80 word = 640 byte/line」
という前提を置いている。

また、640x480 用 `PEGC_GDC_MSYNC_480` / `PEGC_GDC_SSYNC_480` は
資料に完全な表が無かったため、NP21/W の BIOS 実装から採取した値である。

したがって、RA266実機では次のいずれか、または複数が成立していない可能性がある。

- 起動時 GDC PITCH が NP21/W と異なる
- GDC clock 状態が NP21/W と異なる
- master/slave SYNC の固定値が RA266 BIOS の実設定と異なる
- SCROLL / START 順序または値が実機 BIOS と異なる
- 31kHz切替前後で保持される GDC 状態を現行 backend が暗黙に期待している

**PITCH が原因と確定したわけではない。** 修正前に実機状態を採取して比較する。

## 3. 調査手順

### A. RA266 BIOS 起動直後

OS32 が PEGC 初期化を行う前に、可能な範囲で以下を記録する。

- 09A8h 水平走査周波数設定
- GDC master/slave の PITCH
- GDC clock 関連状態
- BIOS work area:
  - 0000:0459h
  - 0000:0597h
  - 0000:054Dh
- master/slave の SYNC / SCROLL に相当する状態

読み戻せない GDC 内部状態は、ブート前 real-mode 段階で BIOS に正規の
640x480 PEGC モードを設定させ、そのとき OS32 が送るべき値を捕捉する方法を検討する。

### B. PEGC init 直後

同じ情報を採取し、A と比較する。
ディスプレイの認識値と、実際の画面崩れ方も記録する。

### C. BIOS 正規設定との比較

可能なら protected mode へ入る前の段階で PC-9821 BIOS の正規画面モード設定を使い、
RA266自身が設定する 640x480 / 31kHz の値を基準として採取する。

NP21/W の固定値より **実機 BIOS の設定を優先する**。

## 4. 修正方針

目標は、PEGC mode set を「起動時 BIOS の残存状態に依存する差分設定」から、
必要な表示条件を明示する **完全な mode set** へ近づけること。

最低でも次を設計対象にする。

- horizontal frequency
- master GDC SYNC
- slave GDC SYNC
- graphics SCROLL
- GDC PITCH
- GDC clock / width interpretation に必要な状態
- VRAM 400/800-line configuration
- PEGC packed-pixel mode
- display START

ただし、実機 BIOS の値を確認せずに PITCH や SYNC を推測で変更しない。

機種別の値が必要と判明した場合は、
「RA266だけを条件分岐で特例化」する前に BIOS work area / capability / mode table から
安全に選べる契約を検討する。

> **下準備 (2026-09-29、wt/pegc480-prep)**: PITCH・GDC クロックを明示して書き、順序を `pegc_apply_timing` 1 か所、
> 値を `include/pegc.h` §10 1 か所に集めた (今は NP21/W 由来の値。実機の `v86 -g` の記録で差し替える)。
> `gdc_send` は FIFO を待つ。詳細と実機の 1 回目の `v86 -g` の結果は
> [TASK_PEGC480_REALHW](../../tasks/realhw/TASK_PEGC480_REALHW.md) §3「段 1 の実機での記録」「段 2 の下準備」・§4。

> **実機 ROM の記録と段 2 (2026-09-29、wt/pegc480-real)**: 実機の `v86 -g` で ROM の INT 18h AH=30h の OUT 列が全部取れた。
> SYNC の値は NP21/W 由来の値と一致 (PITCH・クロックも一致)、違いは順序と OS32 が出していなかったコマンド (RESET・CSRFORM・
> テキスト GDC の PITCH/SCROLL・CRTC・6Eh・表示停止) と SCROLL の LEN。`pegc_apply_timing` を ROM の列に合わせた。比較表と原因は
> [TASK_PEGC480_REALHW](../../tasks/realhw/TASK_PEGC480_REALHW.md) §3-3、v2.1 の受け入れ (画面を見ずに確かめる A/B/C) は同 §5。

## 5. 受入条件

v2.1 の修正完了条件:

1. PC-9821Ra266 実機で PEGC backend を選択し、ディスプレイが 31kHz / 640x480 と認識する。
2. 640x480x256 packed8 の全画面が歪み・横ずれ・行崩れなく表示される。
3. `os32gui` の desktop / window / text / mouse が正常に表示・操作できる。
4. CUI → GUI → CUI 復帰後に元の CUI 表示が正常。
5. NP21/W の PEGC regression を壊さない。
6. 9801 planar / Cirrus backend に回帰がない。
7. 実機で採取した mode state と、採用した定数・設定順序の根拠を本票または `include/pegc.h` に記録する。

## 6. 非目標

- GUI API の変更
- Win98互換層
- Cirrus の仕様変更
- v3 の新API設計
- ディスプレイ固有の「720x350」表示名をOS32側の論理解像度として採用すること

## 7. 現時点の仮説

第一候補は、PEGC backend が **PITCH / GDC clock を起動時 BIOS 状態に依存していること**。
ただし実機採取前は仮説として扱う。

RA266で GUI 起動後にモニタが 31kHz / 640x480 と認識するため、
水平・垂直同期の大枠は変更できている可能性が高い。
その一方で表示内容が崩れることから、VRAMの1行走査幅やGDC master/slave間の表示条件など、
同期周波数以外の mode state を優先して比較する。
