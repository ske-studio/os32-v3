# sdk/rust/os32api/src/gui/ — 設計
> 状態: **現行 (2026-10-31)** — 出所: API_CONTRACTS (D1 で移した)
ソース: `sdk/rust/os32api/src/gui/`

### stub

shlib protocol は2、SHM の `GUI_PROTO_VERSION` は1。ジャンプ表は121本で、
末尾120の `E_TRIM_HOOK_SET` 以外の順序・意味は保つ。旧 protocol 1 は両方向で拒否する。

> [要確認] sdk/rust/os32api/src/gui/stub.rs の現行 SHLIB_NFUNC は 122。121本は記述時点の値。

Rust stub の `init` は OP_INIT 成功後だけ retry を有効化し、アプリ側 trampoline を登録する。
`set_trim_hook(f: fn() -> u32)` は任意のキャッシュ解放 hook を置く。
trampoline はアプリ allocator の IN_TRIM を前後で立て下ろしする。

古い表に当たれば `nfunc` の照合で `bind()` が断る

## 索引

- [sdk/rust/os32api/src/gui/types](types.md)
