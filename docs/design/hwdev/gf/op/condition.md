# hwdev/gf/op/condition.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/op/condition.c`

| opcode・命令名（最終ファイルの語幹） | nouveau の対応 |
|---|---|
| 75 CONDITION | I:1802 `init_condition` |

## 移し替えた設計

| opcode / 名称 | 意味・OS32実装 | MIT根拠 |
|---|---|---|
| 75 CONDITION | 2B、12B条件表の(R32&m)==v、偽ならskip | I:479–494,1802–1813 |


|op / 名前|長さ・復号配置（+offset）|実行substep順・args/kind・方向|辺・展開／skip・force|
|---|---|---|---|
| 75 CONDITION | 2：条件番号8@1→12B表 | Q(t); C`[I(cond),I(mask),I(value)]` | B、(Q&mask)==value |
