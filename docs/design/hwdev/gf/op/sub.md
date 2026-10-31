# hwdev/gf/op/sub.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/op/sub.c`

| opcode・命令名（最終ファイルの語幹） | nouveau の対応 |
|---|---|
| 6B SUB | I:1663 `init_sub` |

SUB の復帰、REPEAT、RESET の強制実行・184C 復元、CR のラッチ復元は各命令試験に残す。

## 移し替えた設計

| opcode / 名称 | 意味・OS32実装 | MIT根拠 |
|---|---|---|
| 6B SUB | 2B、script表のu8番号へcall、DONEでpc+2へ。skip中は呼ばない | I:1663–1683 |

> [要確認] 旧静的展開の substep と現行 shared runner は一致を確認していない。

|op / 名前|長さ・復号配置（+offset）|実行substep順・args/kind・方向|辺・展開／skip・force|
|---|---|---|---|
| 6B SUB | 2：番号8@1 | 入口C`[I(index),I(dst_pc),I(pc+2)]`; 復帰C`[I(pc+2)]` | 5B同様。script表の範囲・入口を検査 |
