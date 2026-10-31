# hwdev/gf/op/zm_cr.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/op/zm_cr.c`

| opcode・命令名（最終ファイルの語幹） | nouveau の対応 |
|---|---|
| 53 ZM_CR | I:1196 `init_zm_cr` |

skip 中の CR44 による head 更新も維持する（`O/kdebug/gfinit_decode.c:79–96`）。

## 移し替えた設計

| opcode / 名称 | 意味・OS32実装 | MIT根拠 |
|---|---|---|
| 53 ZM_CR | 3B、選択headのCRへ直書き | I:1196–1207 |

> [要確認] 旧静的展開の substep と現行 shared runner は一致を確認していない。

|op / 名前|長さ・復号配置（+offset）|実行substep順・args/kind・方向|辺・展開／skip・force|
|---|---|---|---|
| 53 ZM_CR | 3：i8@1,v8@2 | X(t,i); D(t,v) | F、CR44は下記head規則 |
