# hwdev/gf/op/sub_direct.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/op/sub_direct.c`

| opcode・命令名（最終ファイルの語幹） | nouveau の対応 |
|---|---|
| 5B SUB_DIRECT | I:1344 `init_sub_direct` |

SUB の復帰、REPEAT、RESET の強制実行・184C 復元、CR のラッチ復元は各命令試験に残す。

## 索引

- [hwdev/gf/op/](README.md)
