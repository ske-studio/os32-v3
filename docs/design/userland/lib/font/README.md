# userland/lib/font/ — 設計
> 状態: **現行 (2026-10-31)** — 出所: MEMMAP_V3 (D1 で移した)
ソース: `userland/lib/font/`

libos32gui (shlib、text 共有) にラスタライザを置き、**キャッシュはアプリの shlib data / ヒープに私有**

**gshell は libos32gfx を静的リンクしている** (`userland/gshell/Cargo.toml:12`) ので、WM も同じ Rust クレートを静的に持ち、**シェル帯の内側に**私有で持つ

> [要確認] ラスタ層の現行の共有実装は `userland/lib/font/src/`。アプリ側は `userland/rust/libos32gui/src/font.rs`、WM 側は `userland/gshell/src/font.rs` がそれぞれ私有の Engine とキャッシュを持つ。
