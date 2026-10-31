# hwdev/gf/op/end_repeat.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/op/end_repeat.c`

| opcode・命令名（最終ファイルの語幹） | nouveau の対応 |
|---|---|
| 36 END_REPEAT | I:724 `init_end_repeat` |

REPEAT、RESET の強制実行・184C 復元、CR のラッチ復元は各命令試験に残す。

## 移し替えた設計

| opcode / 名称 | 意味・OS32実装 | MIT根拠 |
|---|---|---|
| 36 END_REPEAT | 1B、終端直後を保存し反復フレームへ復帰 | I:724–733 |


|op / 名前|長さ・復号配置（+offset）|実行substep順・args/kind・方向|辺・展開／skip・force|
|---|---|---|---|
| 36 END_REPEAT | 1 | C`[I(repeat_pc),I(k),I(n)]` | Fで次反復入口／最終回は33出口、親ctxへ。skipでも構造を実行 |
