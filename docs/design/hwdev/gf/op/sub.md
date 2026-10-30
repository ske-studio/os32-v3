# hwdev/gf/op/sub.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/op/sub.c`

| opcode・命令名（最終ファイルの語幹） | nouveau の対応 |
|---|---|
| 6B SUB | I:1663 `init_sub` |

SUB の復帰、REPEAT、RESET の強制実行・184C 復元、CR のラッチ復元は各命令試験に残す。
