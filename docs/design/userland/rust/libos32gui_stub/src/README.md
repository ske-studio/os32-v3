# userland/rust/libos32gui_stub/src/ — 設計
> 状態: **現行 (2026-10-31)** — 出所: API_CONTRACTS (D1 で移した)
ソース: `userland/rust/libos32gui_stub/src/`

### lib

Rust stub の `init` は OP_INIT 成功後だけ retry を有効化し、アプリ側 trampoline を登録する。
`set_trim_hook(f: fn() -> u32)` は任意のキャッシュ解放 hook を置く。
trampoline はアプリ allocator の IN_TRIM を前後で立て下ろしする。
