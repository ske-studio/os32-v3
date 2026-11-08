# sdk/rust/os32api/src/gui/ — 設計
> 状態: **現行 (2026-10-31)** — 出所: API_CONTRACTS (D1 で移した)
ソース: `sdk/rust/os32api/src/gui/`

### stub

shlib protocol は2、SHM の `GUI_PROTO_VERSION` は2。ジャンプ表は124本。
既存の0..=121を保ち、122・123にスクロールの口を末尾追記した。
契約と互換規則は [共有ライブラリ設計](../../../../../userland/rust/libos32gui/src/shlib.md) を参照。
旧 protocol 1 は両方向で拒否する。

Rust stub の `init` は OP_INIT 成功後だけ retry を有効化し、アプリ側 trampoline を登録する。
`set_trim_hook(f: fn() -> u32)` は任意のキャッシュ解放 hook を置く。
trampoline はアプリ allocator の IN_TRIM を前後で立て下ろしする。

古い表に当たれば `nfunc` の照合で `bind()` が断る

## 索引

- [sdk/rust/os32api/src/gui/types](types.md)
