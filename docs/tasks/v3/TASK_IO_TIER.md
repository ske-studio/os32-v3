# TASK_IO_TIER — I/O の Tier 制御 (協調点の Tier・予算・設定表・pump)

> 状態: **設計中 (2026-11-14)** — 設計は確定して実装を待つ。詳細設計は [IO_TIER_DESIGN](../../decisions/IO_TIER_DESIGN.md)。
> 関係: 最初の利用者は [TASK_LAN_BULK](../network/TASK_LAN_BULK.md)。時計は [TASK_TICK_1KHZ](TASK_TICK_1KHZ.md) の T1。

## 0. ユーザー決定 (2026-11-14)
| 項目 | 決定 (原文の要旨) |
|---|---|
| 草案 | I/O 処理を 3 段の Tier (Tier1 = PCM 補充・期限の迫った処理、Tier2 = 入力・GUI・ネットワーク、Tier3 = ディスク先読み・キャッシュ) で管理し、優先順位と CPU 実行時間を制御する。起動時の既定の設定表に対し、ユーザーまたはアプリが独自の表を適用できる。初期は固定の CPU 配分率を設けない。V4 では起動像に表を同梱できる構造 |
| U1 (Tier1 の責任) | 「これでもいいですが、アプリ側の責任にしてもいいですがね」→ 推奨どおり初版は案 B: Tier は協調点だけで回し、IRQ 出口は作らない。案 A (出口でも回す) は Tier1 の利用者 (OS のミキサ等) が決まったら別票 |
| U2 (不正な表) | 推奨どおり: 全体を拒否し直前の有効な表を保持、初回は標準、標準への復帰は明示 reset |
| 遅延の合格線 | LAN 一括転送と共通: 4MiB 転送中に入力→応答/表示 p95≤50ms・max≤100ms |

## 1. 詳細設計 (確定) の要約
- 固定 16 枠の登録、Tier ごとの pending の bitmask、空なら即座に戻る (idle_work の互換枠が残る間は互換枠を常に pending と数える)。予算は `io_slice_begin(s, budget_us, max_units)` で生の時計の count に換算する。
- 設定表は 16B の頭 + 16B の行 (LE)、KAPI の io_policy_set/get/reset と io_pump。アプリが入れた表はアプリの終了で戻す (PM 決定)。
- PowerPC は PR=1 から入った syscall の中だけで EE を一時的に許可する口 (arch/ppc/io_pump.c) を 1 つ置き、io_pump と LAN の待ちが共用する。
- 段の表は設計の §7、受入は §8。

## 2. 段の状態
| 段 | 状態 |
|---|---|
| 全段 (§7) | 未着手 |
