# hwdev/gf/op/pll2.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/op/pll2.c`

| opcode・命令名（最終ファイルの語幹） | nouveau の対応 |
|---|---|
| 4B PLL2 | I:985 `init_pll2` |

PLL2/IO_RESTRICT_PLL2 は列全体の成功時だけ解除し、正常 stop で完了済みPLLを戻さない。

## 索引

- [hwdev/gf/op/](README.md)
