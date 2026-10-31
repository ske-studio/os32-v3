# hwdev/gf/op/copy.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/op/copy.c`

| opcode・命令名（最終ファイルの語幹） | nouveau の対応 |
|---|---|
| 37 COPY | I:740 `init_copy` |

CR3C の組を確認する。
> [要確認] CR3C の組の照合は、現行実装では COPY の直後に読み返していない。

## 移し替えた設計

| opcode / 名称 | 意味・OS32実装 | MIT根拠 |
|---|---|---|
| 37 COPY | 11B、VGA←(旧&m) OR (shift(R32)&smask)。shift<80hは右、他は100h−shift左 | I:740–760,531–536 |


|op / 名前|長さ・復号配置（+offset）|実行substep順・args/kind・方向|辺・展開／skip・force|
|---|---|---|---|
| 37 COPY | 11：src32@1,s8@5,sm8@6,p16@7,i8@9,m8@10 | X(dst,i); EXPR e=`[I(dst),R(dst),R(src),I(s),I(sm),I(m)]`/DATA | F。式は§3。W側でdst→src順に採取 |
