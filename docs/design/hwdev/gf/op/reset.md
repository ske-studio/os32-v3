# hwdev/gf/op/reset.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/op/reset.c`

| opcode・命令名（最終ファイルの語幹） | nouveau の対応 |
|---|---|
| 65 RESET | I:1477 `init_reset` |

RESET の強制実行・184C 復元、CR のラッチ復元は各命令試験に残す。
RESET 後 1850 は 0
RESET は184Cを、CR_INDEX_ADDRESS_LATCHED はラッチと元のindexを明示的に復元し、命令完了時に `gf_init_commit` で解除する。

## 索引

- [hwdev/gf/op/](README.md)
