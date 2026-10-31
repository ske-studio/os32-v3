# hwdev/gf/op/zm_index_io.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/op/zm_index_io.c`

| opcode・命令名（最終ファイルの語幹） | nouveau の対応 |
|---|---|
| 62 ZM_INDEX_IO | I:1441 `init_zm_index_io` |

## 移し替えた設計

| opcode / 名称 | 意味・OS32実装 | MIT根拠 |
|---|---|---|
| 62 ZM_INDEX_IO | 5B、indexed VGA直書き | I:1441–1453 |

> [要確認] 旧静的展開の substep と現行 shared runner は一致を確認していない。

|op / 名前|長さ・復号配置（+offset）|実行substep順・args/kind・方向|辺・展開／skip・force|
|---|---|---|---|
| 62 ZM_INDEX_IO | 5：p16@1,i8@3,v8@4 | X(t,i); D(t,v) | F |
