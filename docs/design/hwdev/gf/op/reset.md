# hwdev/gf/op/reset.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/op/reset.c`

| opcode・命令名（最終ファイルの語幹） | nouveau の対応 |
|---|---|
| 65 RESET | I:1477 `init_reset` |

RESET の強制実行・184C 復元、CR のラッチ復元は各命令試験に残す。
RESET 後 1850 は 0
RESET は184Cを、CR_INDEX_ADDRESS_LATCHED はラッチと元のindexを明示的に復元し、命令完了時に `gf_init_commit` で解除する。

## 移し替えた設計

| opcode / 名称 | 意味・OS32実装 | MIT根拠 |
|---|---|---|
| 65 RESET | 13B、force。184Cの0F00を退避/クリア→regへdata1→10µs→data2→184C復元→1850 bit0クリア | I:1477–1497 |


|op / 名前|長さ・復号配置（+offset）|実行substep順・args/kind・方向|辺・展開／skip・force|
|---|---|---|---|
| 65 RESET | 13：reg32@1,d1@5,d2@9 | M(184C,FFFFF0FF,0;WRITE); L(reg,d1); C`[I(10)]`; L(reg,d2); `[I(184C),S(q)]`/WRITE; M(1850,FFFFFFFE,0;WRITE) | e=14,15,16,17,18。Cは10µs待ち、最初のM内部READを採取。skipでも全列FORCE、終了後skip保持 |
