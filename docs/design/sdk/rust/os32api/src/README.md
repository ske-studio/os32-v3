# sdk/rust/os32api/src/ — 設計
> 状態: **現行 (2026-10-31)** — 出所: API_CONTRACTS (D1 で移した)
ソース: `sdk/rust/os32api/src/`

### lib

Rust の retry / retry成功 / hook page hint は `os32api::gui::retry_stats()` で観測する。

Rust の `Os32Alloc` の size 0 は NULL を返し、`mem_alloc(0)` を呼ばず、再試行しない。

## 索引

- [sdk/rust/os32api/src/gui/README](gui/README.md)
