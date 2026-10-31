# hwdev/gf/op/nv_reg.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/op/nv_reg.c`

| opcode・命令名（最終ファイルの語幹） | nouveau の対応 |
|---|---|
| 6E NV_REG | I:1709 `init_nv_reg` |

## 移し替えた設計

| opcode / 名称 | 意味・OS32実装 | MIT根拠 |
|---|---|---|
| 6E NV_REG | 13B、R32←(旧&m) OR 値 | I:1709–1720 |


|op / 名前|長さ・復号配置（+offset）|実行substep順・args/kind・方向|辺・展開／skip・force|
|---|---|---|---|
| 6E NV_REG | 13：reg32@1,m32@5,v32@9 | M(t,m,v;WRITE) | F、keep=全1/value=0も省略しない |
