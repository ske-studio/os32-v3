# hwdev/gf/runner.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/runner.c`

runner が解読後・dispatch 前に REPEAT の `end_repeat` を対応する END_REPEAT PC で補完し、run は body-PC/end-PC/残り回数を frame に置く。命令内の待ちはすべて `gf_init_wait` に通し、累積待ちの一括計上・1000us 以下の分割・各刻みの前後の中断点/期限検査は runner が実装する。
既存の命令/呼出し/反復/累積待ち上限を保ち、命令と wait の刻みに中断点を置く。診断の main 間承認待ちは本番に持ち込まない。
本番全体のソフト上限は既存 `GF_INIT_EXEC_US` の 60 秒を採用（`O/kdebug/gfinit_exec.h:4`）。期限超過は既存失敗出口へ進むが、戻らない MMIO に壁時計上限は保証できない。
旧 plan executor の丸ごと移植や恒久的な二重実装はしない。
実 ROM fixture（27 種/193 命令）の旧展開との I/O trace 差分試験 1 本＋制御個別 case
skip/head、呼出し/反復上限
## 索引

- [hwdev/gf/](README.md)
- [hwdev/gf/op/](op/README.md)
- [hwdev/gf/internal.h](internal.md)
