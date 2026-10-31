# hwdev/gf/op/io_flag_condition.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/op/io_flag_condition.c`

| opcode・命令名（最終ファイルの語幹） | nouveau の対応 |
|---|---|
| 39 IO_FLAG_CONDITION | I:779 `init_io_flag_condition` |

## 移し替えた設計

| opcode / 名称 | 意味・OS32実装 | MIT根拠 |
|---|---|---|
| 39 IO_FLAG_CONDITION | 2B、9B条件表参照、偽ならskip | I:512–528,779–790 |

> [要確認] 旧静的展開の substep と現行 shared runner は一致を確認していない。

|op / 名前|長さ・復号配置（+offset）|実行substep順・args/kind・方向|辺・展開／skip・force|
|---|---|---|---|
| 39 IO_FLAG_CONDITION | 2：条件番号8@1→9B表 | X(t,i); Q(t); C`[I(cond),I(m),I(s),I(data_pc),I(dm),I(v)]` | B、ROM[data+((Q&m)>>s)]&dm==v。全indexのROM範囲を検査 |
