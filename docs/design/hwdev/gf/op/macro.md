# hwdev/gf/op/macro.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/op/macro.c`

| opcode・命令名（最終ファイルの語幹） | nouveau の対応 |
|---|---|
| 6F MACRO | I:1727 `init_macro` |

## 移し替えた設計

| opcode / 名称 | 意味・OS32実装 | MIT根拠 |
|---|---|---|
| 6F MACRO | 2B、macro表+index×8の{reg,value}を1組書く。4件の範囲検査 | I:1727–1743 |


|op / 名前|長さ・復号配置（+offset）|実行substep順・args/kind・方向|辺・展開／skip・force|
|---|---|---|---|
| 6F MACRO | 2：index8@1→表8B | L(t,v) | 1組だけ展開、表のindex×8を検査。反復時e再利用・ctx別 |
