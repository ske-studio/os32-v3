# hwdev/gf/op/cr_index_address_latched.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/op/cr_index_address_latched.c`

| opcode・命令名（最終ファイルの語幹） | nouveau の対応 |
|---|---|
| 51 CR_INDEX_ADDRESS_LATCHED | I:1146 `init_cr_idx_adr_latch` |

CR のラッチ復元は各命令試験に残す。
CR_INDEX_ADDRESS_LATCHED はラッチと元のindexを明示的に復元し、命令完了時に `gf_init_commit` で解除する。

## 索引

- [hwdev/gf/op/](README.md)
