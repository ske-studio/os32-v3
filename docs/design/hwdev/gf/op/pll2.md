# hwdev/gf/op/pll2.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/op/pll2.c`

| opcode・命令名（最終ファイルの語幹） | nouveau の対応 |
|---|---|
| 4B PLL2 | I:985 `init_pll2` |

PLL2/IO_RESTRICT_PLL2 は列全体の成功時だけ解除し、正常 stop で完了済みPLLを戻さない。

## 移し替えた設計

| opcode / 名称 | 意味・OS32実装 | MIT根拠 |
|---|---|---|
| 4B PLL2 | 9B、regへu32 kHzをsolverとチップ別手順で設定 | I:985–996 |

> [要確認] 旧静的展開の substep と現行 shared runner は一致を確認していない。

|op / 名前|長さ・復号配置（+offset）|実行substep順・args/kind・方向|辺・展開／skip・force|
|---|---|---|---|
| 4B PLL2 | 9：reg32@1,kHz32@5 | PLL入口C`[I(reg),I(kHz),I(limits_pc)]` | F→S4内部列。未実装を成功NONEにしない |
