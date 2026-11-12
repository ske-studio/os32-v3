# CONSOLE_DUAL — GUI の間も PC-98 の TVRAM にコンソールを出し続ける (2 画面・3 画面)

> 状態: **実装中 (2026-11-12)** — 設計は確定 ([CONSOLE_DUAL_DESIGN](../../decisions/gui/CONSOLE_DUAL_DESIGN.md))、11-12 に main 15738b50 と照らし直して D1g を廃止し HAL v2 の K1 を D2 に吸収 (設計の末尾の追記)。段は H0・D1 → D2〜D5 (並列) → D6、全部を 1 本の統合の枝で取り込む。 2026-11-12: main eebdafaa で実機 A6 の記録の消去は直った (端末の ver が出なかったのは試験の操作の取りこぼし)。CUI へ戻る時に右のモニターが切れない件は、GF の停止の道の DPMS ON が手本に無い処理だったため、停止を手本 nouveau に寄せる ([GF_STOP_REF_DESIGN](../../decisions/realhw/GF_STOP_REF_DESIGN.md)、ユーザー決定「手本どおりに寄せる」) 段で直す。

## 0. ユーザー決定 (原文)
- 2026-11-09 HAL v2 の 2 枚同時の出力 (PM 決定 #3 の確定): 「AとC」 — C = ACTIVE でない側 (PC-98 内蔵) の TVRAM に CUI・診断を出し続ける。
- 2026-11-09 対象の構成: 「この時しか使わないです。2画面。もっと言うとGeforceのDVIも足して3画面でも大丈夫なように。結線の件は気にしないで」 — GeForce の D-SUB を別のモニターに直結し 0FACh は 98 のままの構成だけ。GF の DVI を足した 3 画面でも破綻しない形。実機の受入も入れる (結線はユーザー)。
- Trident (内蔵、98 と同じ端子) は対象外。

## 1. 詳細設計 (確定) の要約
全文は [CONSOLE_DUAL_DESIGN](../../decisions/gui/CONSOLE_DUAL_DESIGN.md)。

| 段 | ファイル | 前提 |
|---|---|---|
| H0 | gfx/v2/enum.c (gfx2_head_info が v1 の机の connector_mask を 0 で上書きする欠けの直し) | — |
| D1 | kernel/console/console_dual.h・.c (新、出力先の表 N 個と判定) | — |
| D2 | platform/pc98/display_dual.c (新、gfx2 の列挙で表を作る・GUI の状態に触らない文字画面の開始) | D1・H0 |
| D3〜D5 | console.c の門 4 か所・pc98_route.c・init/kernel.c (412・473) | D1 (D5 は D3 も) |
| D6 | tools/tests/console_dual_host.c と `make check-console-dual-host` | D1〜D4 |

- 設定 `CONSOLE_DUAL` (system.cfg) の既定は 0。
- 一覧は能力として GF の全 head を常に返し (98 と合わせて 3 行)、停止中の head は running=0。接続の判定は保存した EDID の検出 (base_valid) まで、抜き差しのライブ検出はしない。
- PowerPC 第 5 段 C1 (console.c) は main にあり、D3 はその上に載せる。
- GF の行は最初から gfx2 の列挙 (HAL v2 の K1 を D2 に吸収、gf_outputs は作らない)。
