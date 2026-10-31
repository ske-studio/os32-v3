# hwdev/gf/display/load_row.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/display/load_row.c`

PLL2/IO_RESTRICT_PLL2 と P3 PLL は列全体の成功時だけ解除し、正常 stop で完了済みPLLを戻さない。
B は `int gf_load_row(struct gf_display_context *, const struct gfinit_mode *, u32 ordinal)` を `gf_load_io.row` へ接続し、section/wait も既存型を使う（`O/hwdev/gf_load_state.h:36–41`）。
AR の FF reset→index→data、DAC の write-only auto-increment、LIVE RMW の実行時読取り、既存の幅/範囲/有限待ちを保持する。
移動した各機能の既存試験、AR/DAC/LIVE/FP/PLL復元・独立期待列・CHECK

## 移し替えた設計

6813C6=FF→6813C8=00→6813C9にRGB×256。

各index/data前にFF reset；AR gate復帰→unprotect
