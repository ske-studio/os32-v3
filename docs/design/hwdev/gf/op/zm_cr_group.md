# hwdev/gf/op/zm_cr_group.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/op/zm_cr_group.c`

| opcode・命令名（最終ファイルの語幹） | nouveau の対応 |
|---|---|
| 54 ZM_CR_GROUP | I:1213 `init_zm_cr_group` |

skip 中の CR44 による head 更新も維持する（`O/kdebug/gfinit_decode.c:79–96`）。

## 移し替えた設計

| opcode / 名称 | 意味・OS32実装 | MIT根拠 |
|---|---|---|
| 54 ZM_CR_GROUP | 2+2count B、{index,data}のCR直書き | I:1213–1230 |


|op / 名前|長さ・復号配置（+offset）|実行substep順・args/kind・方向|辺・展開／skip・force|
|---|---|---|---|
| 54 ZM_CR_GROUP | 2+2n：n8@1,{i8,v8}[]@2 | 各要素X(t,i); D(t,v) | 各要素をe別nodeに展開、head変化は次要素にも伝播 |
