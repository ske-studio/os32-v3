# TASK_P8_GUI_LAYER — P8 GUI 層 (音のリングと入力)

> 状態: **設計中 (2026-11-05)** — 設計は確定、実装はまだ出していない。

## 0. ユーザー決定 (原文)

- 「microUIは廃止です。すでにos32guiがあるので」(2026-11-05)。8bpp のまま、音 (PCM リング) と入力の層を作る。
- 設計を回す指示: 「実装はとりあえず良いですが、設計は回してください。」(2026-11-05)

## 1. 詳細設計 (確定) の要約

全文: [P8_GUI_LAYER_DESIGN](../../decisions/gui/P8_GUI_LAYER_DESIGN.md) (確定の節が本文より優先)。未決は 0 件。

| 段 | 中身 | 状態 |
|---|---|---|
| A | PCM 側: `pcm_reclaim` の内部停止関数への抽出、drain_begin/drain_poll、private 表の末尾追加、`pcm_stream.c` (公開の 4 本 pcm_start・pcm_finish・pcm_abort・pcm_stream_status) | 未着手 |
| B | 入力側: gshell の `input_queue.rs` (append 失敗の add_dropped)・`input.rs` の統一 emit・OVERFLOW 時の入力取消 | 未着手。A と並列可 |
| C | KAPI の末尾追加・版上げ・生成物の追従 ([ABI]) | 未着手 |
| D | host 試験 (`pcm_stream_host`・`input_queue_tests`) と変異、NP21/W・実機の受入 | 未着手 |

- 入口: 段 A (PCM の口を固定してから)。PCM module の対の版一致を要求する。
- PM 決定 (確定の節): `pcm_cs4231.c`・`pcm_module.*`・`pcm_body.c` は P8 の段 A を先に取り込み、[PCM の合成器](../v3/TASK_SYNTH_OPNA.md) の段 E はその後に末尾へ足す。合成器の finish/abort も同じ内部停止・drain の経路を使う。
- WM のマウスキー (`kbdnav.rs`) は変更しない。microUI の接続は実装しない。
- 実装時は `docs/design/<ソースパス>.md` へ機能別に分割する。
