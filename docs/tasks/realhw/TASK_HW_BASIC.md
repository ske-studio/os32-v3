# TASK_HW_BASIC — ハードウェア調査の道具 HW-BASIC (汎用、許可リストなし)

> 状態: **実装中 (2026-11-05)** — 設計 ([HW_BASIC_DESIGN](../../decisions/realhw/HW_BASIC_DESIGN.md)) どおりの実装は main (a9819c78、CI で取り込み)。実機 Ra266 で `hw run /test/hw/trident.hwb` の mode=0/1/2 を流し、止まらずに通った (結果は TASK_TRIDENT_DRIVER §2-1 の 11-05)。次の段: `mmiofill` の追加 (TRIDENT_V6_DESIGN の段 2a、wt/trident-2a で実装中)。

## 0. ユーザー決定 (原文)

- 2026-10-21「Geforce が終わった後の Trident で実験しましょう」
- 2026-11-04「確認して実装前にはgeforceと同様の道具を用意してください。なるべくソースの改変なくテストと書き込みが出来る仕組みで」
- 2026-11-04「設計は他のPCIデバイスにも使えるように」
- 2026-11-04「ソースに追加しないと拒否する仕組みはやめましょう。安全性のためと思いますが、シングルタスクOSでは不要です」

## 1. 要約

- シェルの `hw` で 1 行ずつ、または `hw run <file>.hwb` でスクリプトを流す。PCI の構成空間・I/O・MMIO (BAR の番号と位置、任意の物理番地) の読み書き、時間とビットの待ち、読み・変更・書き、記録 (`HW op=…` の 1 行)。
- 特権の操作は kdebug の op (HW_OP=18) で単発ずつ。待ちはシェルの側。中止は CTRL+STOP (CUI のシェルの実行中も効くようにカーネルに小さな印を足す)。
- peek -m の許可リスト (kdebug/kpeek_allow.c) は撤去する。最初の題材は Trident (X.Org xf86-video-trident 1.4.0 の PC98TRIDENT96xx を写したスクリプト)。
- 段の表・試験・変異は確定設計の §6・§7。
