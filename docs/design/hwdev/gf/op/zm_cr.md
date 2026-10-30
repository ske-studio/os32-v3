# hwdev/gf/op/zm_cr.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/op/zm_cr.c`

| opcode・命令名（最終ファイルの語幹） | nouveau の対応 |
|---|---|
| 53 ZM_CR | I:1196 `init_zm_cr` |

skip 中の CR44 による head 更新も維持する（`O/kdebug/gfinit_decode.c:79–96`）。
