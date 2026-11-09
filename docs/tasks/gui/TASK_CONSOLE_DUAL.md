# CONSOLE_DUAL — GUI の間も PC-98 の TVRAM にコンソールを出し続ける (2 画面・3 画面)

> 状態: **設計中 (2026-11-09)** — 設計は確定、実装を待つ ([CONSOLE_DUAL_DESIGN](../../decisions/gui/CONSOLE_DUAL_DESIGN.md))。段は D1・D1g → D2〜D5 (並列) → D6、D1〜D6 を 1 本の統合の枝で取り込む。PM は設計に専念中のため実装の着手はユーザーの解除の後。

## 0. ユーザー決定 (原文)
- 2026-11-09 HAL v2 の 2 枚同時の出力 (PM 決定 #3 の確定): 「AとC」 — C = ACTIVE でない側 (PC-98 内蔵) の TVRAM に CUI・診断を出し続ける。
- 2026-11-09 対象の構成: 「この時しか使わないです。2画面。もっと言うとGeforceのDVIも足して3画面でも大丈夫なように。結線の件は気にしないで」 — GeForce の D-SUB を別のモニターに直結し 0FACh は 98 のままの構成だけ。GF の DVI を足した 3 画面でも破綻しない形。実機の受入も入れる (結線はユーザー)。
- Trident (内蔵、98 と同じ端子) は対象外。

## 1. 詳細設計 (確定) の要約
全文は [CONSOLE_DUAL_DESIGN](../../decisions/gui/CONSOLE_DUAL_DESIGN.md)。

| 段 | ファイル | 前提 |
|---|---|---|
| D1 | kernel/console/console_dual.h・.c (新、出力先の表 N 個と判定) | — |
| D1g | drivers/video/geforce/gf_outputs.h・.c (新、HAL v2 C1 まで暫定の head の能力の口) | D1 の .h |
| D2 | platform/pc98/display_dual.c (新、GUI の状態に触らない文字画面の開始) | D1・D1g |
| D3〜D5 | console.c の門 4 か所・pc98_route.c・init/kernel.c (412・473) | D1 (D5 は D3 も) |
| D6 | tools/tests/console_dual_host.c と `make check-console-dual-host` | D1〜D4 |

- 設定 `CONSOLE_DUAL` (system.cfg) の既定は 0。
- 一覧は能力として GF の全 head を常に返し (98 と合わせて 3 行)、停止中の head は running=0。接続の判定は保存した EDID の検出 (base_valid) まで、抜き差しのライブ検出はしない。
- PowerPC 第 5 段 C1 (console.c) と重なるのは D3 だけ。先に入った方に後が載せ直す (推奨 D3 が先)。
- HAL v2 C1 で gf_outputs を消し、display_dual.c の GF の行を gfx2 の列挙に差し替える。
