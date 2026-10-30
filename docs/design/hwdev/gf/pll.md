# hwdev/gf/pll.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/pll.c`

PLL2/IO_RESTRICT_PLL2 と P3 PLL は列全体の成功時だけ解除し、正常 stop で完了済みPLLを戻さない。
共通 solver は一つ。
PLL 26/7/1/1/2 と失敗点を重点化する。
