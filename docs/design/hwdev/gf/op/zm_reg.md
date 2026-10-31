# hwdev/gf/op/zm_reg.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/op/zm_reg.c`

| opcode・命令名（最終ファイルの語幹） | nouveau の対応 |
|---|---|
| 7A ZM_REG | I:1892 `init_zm_reg` |

PMC_ENABLE bit0 を立てる

## 移し替えた設計

| opcode / 名称 | 意味・OS32実装 | MIT根拠 |
|---|---|---|
| 7A ZM_REG | 9B、R32直書き。宛先200hだけbit0をOR | I:1892–1906 |

> [要確認] 旧静的展開の substep と現行 shared runner は一致を確認していない。

|op / 名前|長さ・復号配置（+offset）|実行substep順・args/kind・方向|辺・展開／skip・force|
|---|---|---|---|
| 7A ZM_REG | 9：reg32@1,v32@5 | L(t, reg==200h ? v OR 1 : v) | F、200h補正はこのopcodeだけ（RESET値へ適用しない） |
