# hwdev/gf/op/resume.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/op/resume.c`

| opcode・命令名（最終ファイルの語幹） | nouveau の対応 |
|---|---|
| 72 RESUME | I:1751 `init_resume` |

## 移し替えた設計

| opcode / 名称 | 意味・OS32実装 | MIT根拠 |
|---|---|---|
| 72 RESUME | 1B、条件skipクリア | I:1751–1756 |


|op / 名前|長さ・復号配置（+offset）|実行substep順・args/kind・方向|辺・展開／skip・force|
|---|---|---|---|
| 72 RESUME | 1 | C`[]` | F、skip=0（skip中も実行） |
