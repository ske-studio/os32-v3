# hwdev/gf/op/time.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/op/time.c`

| opcode・命令名（最終ファイルの語幹） | nouveau の対応 |
|---|---|
| 74 TIME | I:1781 `init_time` |

## 移し替えた設計

| opcode / 名称 | 意味・OS32実装 | MIT根拠 |
|---|---|---|
| 74 TIME | 3B、u16 µs。実行許可時だけ待つ。§5の時計で最低指定時間を保証 | I:1781–1795 |

> [要確認] 旧静的展開の substep と現行 shared runner は一致を確認していない。

|op / 名前|長さ・復号配置（+offset）|実行substep順・args/kind・方向|辺・展開／skip・force|
|---|---|---|---|
| 74 TIME | 3：µs16@1 | C`[I(us)]` | F、skipはKで待ち0。待ち累計/動的予算へ加算 |
