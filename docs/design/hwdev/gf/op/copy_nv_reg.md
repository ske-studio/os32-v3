# hwdev/gf/op/copy_nv_reg.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/op/copy_nv_reg.c`

| opcode・命令名（最終ファイルの語幹） | nouveau の対応 |
|---|---|
| 5F COPY_NV_REG | I:1415 `init_copy_nv_reg` |

## 移し替えた設計

| opcode / 名称 | 意味・OS32実装 | MIT根拠 |
|---|---|---|
| 5F COPY_NV_REG | 22B、dst←(旧&dm) OR ((shift(src)&sm)^xor)。10020Cは読取り元、1700が宛先 | I:1415–1434 |


|op / 名前|長さ・復号配置（+offset）|実行substep順・args/kind・方向|辺・展開／skip・force|
|---|---|---|---|
| 5F COPY_NV_REG | 22：src32@1,s8@5,sm32@6,xor32@10,dst32@14,dm32@18 | EXPR e=`[I(dst),R(src),R(dst),I(s),I(sm),I(xor),I(dm)]`/WRITE | F。src→dst順に採取。式は§3 |
