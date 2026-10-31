# hwdev/gf/op/zm_reg_sequence.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/op/zm_reg_sequence.c`

| opcode・命令名（最終ファイルの語幹） | nouveau の対応 |
|---|---|
| 58 ZM_REG_SEQUENCE | I:1281 `init_zm_reg_sequence` |

## 移し替えた設計

| opcode / 名称 | 意味・OS32実装 | MIT根拠 |
|---|---|---|
| 58 ZM_REG_SEQUENCE | 6+4count B、baseから4ずつ増やしてu32直書き。実物count=3/4/4、11レジスタ | I:1281–1300 |

> [要確認] 旧静的展開の substep と現行 shared runner は一致を確認していない。

|op / 名前|長さ・復号配置（+offset）|実行substep順・args/kind・方向|辺・展開／skip・force|
|---|---|---|---|
| 58 ZM_REG_SEQUENCE | 6+4n：base32@1,n8@5,v32[]@6 | 各kにL(base+4k,v[k]) | 実n=3/4/4、11 W。checked番地計算 |
