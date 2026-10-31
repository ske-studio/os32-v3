# hwdev/gf/op/io_restrict_pll2.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/op/io_restrict_pll2.c`

| opcode・命令名（最終ファイルの語幹） | nouveau の対応 |
|---|---|
| 4A IO_RESTRICT_PLL2 | I:950 `init_io_restrict_pll2` |

PLL2/IO_RESTRICT_PLL2 は列全体の成功時だけ解除し、正常 stop で完了済みPLLを戻さない。

## 移し替えた設計

| opcode / 名称 | 意味・OS32実装 | MIT根拠 |
|---|---|---|
| 4A IO_RESTRICT_PLL2 | 11+4count B、VGA選択のu32 kHzでPLL設定。範囲外は無書込みで記録・続行 | I:950–978 |


|op / 名前|長さ・復号配置（+offset）|実行substep順・args/kind・方向|辺・展開／skip・force|
|---|---|---|---|
| 4A IO_RESTRICT_PLL2 | 11+4n：32と同配置（値はkHz） | X(t,i); Q(t); 各kにC`[I(m),I(s),I(k),I(n)]`; PLL入口C`[I(reg),I(kHz),I(limits_pc)]` | J。PLL内部101候補はS4、§4。実物の同一周波数は同一PLL入口へ合流可能 |
