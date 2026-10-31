# userland/rust/libos32gui/src/shlib.rs — 設計
> 状態: **現行 (2026-10-31)** — 出所: API_CONTRACTS (D1 で移した)
ソース: `userland/rust/libos32gui/src/shlib.rs`

shlib protocol は2、SHM の `GUI_PROTO_VERSION` は1。ジャンプ表は121本で、
末尾120の `E_TRIM_HOOK_SET` 以外の順序・意味は保つ。旧 protocol 1 は両方向で拒否する。

> [要確認] 現行の `sdk/rust/os32api/src/gui/stub.rs:282` と `userland/rust/libos32gui/src/shlib.rs:217` はジャンプ表を 122 本としている。

複数行は**新しい種別**を末尾に足した。ジャンプ表も 111..=117 への末尾追記だけで、
`GUI_PROTO_VERSION` は据え置き (古いアプリは新しい表でもそのまま動き、新しいアプリが
古い表に当たれば `nfunc` の照合で `bind()` が断る)。

## 互換上の注意と改訂記録

`os32gui_w_focused` (entry 77) は窓スロット添字を取る既存 ABI を維持する。
アプリは WindowId を取る `os32gui_w_focused_in` を使う。定義は
[`shlib.rs:1171・1183–1187`](../../../../../../userland/rust/libos32gui/src/shlib.rs)。
