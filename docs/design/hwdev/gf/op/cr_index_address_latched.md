# hwdev/gf/op/cr_index_address_latched.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/op/cr_index_address_latched.c`

| opcode・命令名（最終ファイルの語幹） | nouveau の対応 |
|---|---|
| 51 CR_INDEX_ADDRESS_LATCHED | I:1146 `init_cr_idx_adr_latch` |

CR のラッチ復元は各命令試験に残す。
CR_INDEX_ADDRESS_LATCHED はラッチと元のindexを明示的に復元し、命令完了時に `gf_init_commit` で解除する。

## 移し替えた設計

| opcode / 名称 | 意味・OS32実装 | MIT根拠 |
|---|---|---|
| 51 CR_INDEX_ADDRESS_LATCHED | 5+count B、CR addr0を保存、base++をaddr0へ、dataをaddr1へ書き、addr0復元 | I:1146–1168 |

> [要確認] 旧静的展開の substep と現行 shared runner は一致を確認していない。

|op / 名前|長さ・復号配置（+offset）|実行substep順・args/kind・方向|辺・展開／skip・force|
|---|---|---|---|
| 51 CR_INDEX_ADDRESS_LATCHED | 5+n：a0@1,a1@2,base@3,n@4,byte[]@5 | 最初X(t0,a0); Q(t0); 各kでD(t0,base+k), X(t1,a1), D(t1,data[k]); 次回前X(t0,a0); 最終X(t0,a0), `[I(t0),S(q)]`/DATA | 最初のXは既存先頭eを兼用。n=7/3、最終e=410/454。採取と再生は§3。base加算はu8範囲確認 |
