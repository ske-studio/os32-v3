# userland/rust/gui_bench/src/ — 設計
> 状態: **現行 (2026-10-31)** — 出所: API_CONTRACTS (D1 で移した)
ソース: `userland/rust/gui_bench/src/`

### lib

`/api/key` からの入力について、その `serial` の取り込み tick から `commit`
完了までの差 (WM とリングの待ち時間を含む「入力 → 表示」) と `gfx_stats` の差分を tvram に
出す。
