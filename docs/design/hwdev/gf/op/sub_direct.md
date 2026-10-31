# hwdev/gf/op/sub_direct.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/op/sub_direct.c`

| opcode・命令名（最終ファイルの語幹） | nouveau の対応 |
|---|---|
| 5B SUB_DIRECT | I:1344 `init_sub_direct` |

SUB の復帰、REPEAT、RESET の強制実行・184C 復元、CR のラッチ復元は各命令試験に残す。

## 移し替えた設計

| opcode / 名称 | 意味・OS32実装 | MIT根拠 |
|---|---|---|
| 5B CALL（SUB_DIRECT） | 3B、ROM内u16絶対先へcall、DONEでpc+3へ。skip中は呼ばない | I:1344–1363 |

> [要確認] 旧静的展開の substep と現行 shared runner は一致を確認していない。

|op / 名前|長さ・復号配置（+offset）|実行substep順・args/kind・方向|辺・展開／skip・force|
|---|---|---|---|
| 5B CALL / SUB_DIRECT | 3：絶対pc16@1 | 入口C`[I(dst_pc),I(pc+3)]`; 復帰C`[I(pc+3)]` | 呼出箇所別展開、新ctx→DONE→親ctx復帰。skipはKのみ（非呼出し） |
