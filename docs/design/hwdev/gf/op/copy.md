# hwdev/gf/op/copy.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/op/copy.c`

| opcode・命令名（最終ファイルの語幹） | nouveau の対応 |
|---|---|
| 37 COPY | I:740 `init_copy` |

CR3C の組を確認する。
> [要確認] CR3C の組の照合は、現行実装では COPY の直後に読み返していない。

## 索引

- [hwdev/gf/op/](README.md)
