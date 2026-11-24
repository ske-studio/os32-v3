# TASK_SYNTH_OPNA — PCM の合成器 (ソフト OPNA、fmgen の移植)

> 状態: **実装中 (2026-11-24)** — コア (lib/os32_opna の段 A〜D、元の fmgen と全サンプル一致)・libos32snd の L1・L2・L5・アプリからの読み込み (LD1〜LD3) は手元の枝 (wip/synth-ld ほか、push していない)。x86 は NP21/W で sndl_test が通り、PowerPC は QEMU のゲスト試験が通った。鳴らす段 (L3・L6) は AV_HAL の S 段の後。

## 0. ユーザー決定 (原文)

- 合成器: 「fmgenは採用です」「fmgenは無理に手本でなく移植で大丈夫です」(2026-11-04)
- 設計を回す指示: 「実装はとりあえず良いですが、設計は回してください。」(2026-11-05)
- 設計の未決 3 件 (2026-11-05、原文):「7 件とも推奨どおり (推奨)」。中身: (a) リズム 6 音の素材は同梱しない (b) FM 音源が OPN だけの機械の既定はハードの OPN (c) V86 のタイマの割り込みは PCM のタイマで次の期限に起こす (実装前に資料で確かめ、だめなら PIT の one-shot、どちらも駄目なら 100Hz の上限を改めて諮る)。
- 着手と進め方 (2026-11-24):「着手」「完成までプッシュせず自前で通るところまで実装してください」「並列実装は？」。PM の解釈: 合成器の枝は完成 (鳴らす段まで) まで push せず手元でビルド・試験・レビューを通し、完成した時にまとめて報告してから push する。段 C は 3 本を並べて作る。
- 効果音 (FM 音源で鳴らす、PCM だけの機械では fmgen 由来の PCM をユーザーが任意で実行したときに作る) は 2026-11-04 のユーザー決定 (V3_PLAN の該当行)。起動時には作らない。

## 1. 詳細設計 (確定) の要約

全文: [SYNTH_OPNA_DESIGN](../../decisions/sound/SYNTH_OPNA_DESIGN.md) (確定の節が本文より優先)。

| 段 | 中身 | 状態 |
|---|---|---|
| A | `lib/os32_opna/` の ABI ヘッダ・PORT.md・fmgen の 4 原文と README.OS32・THIRD_PARTY.md | 未着手 |
| B | 元 C++ の表の生成 (`gen_opna_tables`)・状態 (state.rs) | 未着手 |
| C | operator・channel・psg・adpcm・rhythm・timer (Rust、整数演算) | 未着手 |
| D | registers・lib.rs (FFI・混合)・Cargo・target・`synth-module.mk` | 未着手 |
| E | `drivers/sound/synth/` (供給・通常文脈の pump・WAV 読込み・常駐 facade) と `pcm_source.inc` (内部の口 attach・push・finish・abort・status・detach) | 未着手。P8 の段 A を先に取り込む |
| F | `drivers/sound/fm/` の backend (probe・auto/hw/soft/none)・`snd_engine.c` の接続 | 未着手 |
| G | `kernel/v86/v86_opna.c` ほか (A460h の仮想化・タイマ)・`synth_timer.c` | 未着手。タイマの起床源は実装前に資料で確かめる |
| H | host の oracle (元 C++ との差分)・変異・`check_opna`・ゲスト試験 | 未着手 |

- 入口: 段 A から。段 E は P8 GUI 層 (票 TASK_P8_GUI_LAYER) の段 A の後 (PM 決定)。
- PM 決定 (確定の節): 世代付き handle と STALE は使わない (token と owner)、タイマの FIFO 積みは通常文脈の trampoline へ。
- 浮動小数点は使わない (Rust の float_arithmetic を deny、全対象の台帳は PORT.md)。fmgen の条件 (出所・フリー配布・改変の明示・原文添付) は A 段で満たす。
- 受入 (host / NP21/W / 実機) は全文の §9。実機 Ra266 の OPNA 判定は実機受入に回す。
