# hwdev/gf/op/zm_cr_group.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/op/zm_cr_group.c`

| opcode・命令名（最終ファイルの語幹） | nouveau の対応 |
|---|---|
| 54 ZM_CR_GROUP | I:1213 `init_zm_cr_group` |

skip 中の CR44 による head 更新も維持する（`O/kdebug/gfinit_decode.c:79–96`）。
