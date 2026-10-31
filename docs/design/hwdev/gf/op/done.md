# hwdev/gf/op/done.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/op/done.c`

| opcode・命令名（最終ファイルの語幹） | nouveau の対応 |
|---|---|
| 71 DONE | I:610 `init_done` |

## 移し替えた設計

| opcode / 名称 | 意味・OS32実装 | MIT根拠 |
|---|---|---|
| 71 DONE | 1B、現在フレーム終了、callなら呼出元、mainなら要求終了 | I:610–614 |

> [要確認] 旧静的展開の substep と現行 shared runner は一致を確認していない。

|op / 名前|長さ・復号配置（+offset）|実行substep順・args/kind・方向|辺・展開／skip・force|
|---|---|---|---|
| 71 DONE | 1 | C`[]` | callなら復帰node、mainなら正常終端（入口順の次mainは別承認）。skipでも構造実行 |
