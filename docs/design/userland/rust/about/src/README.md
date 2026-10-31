# userland/rust/about/src/ — 設計
> 状態: **現行 (2026-10-31)** — 出所: VER_INFO_DESIGN (D1 で移した)
ソース: `userland/rust/about/src/`

### info

- GUI は一回取得し、kind の低8bitで区切りを配置、INDENT は表示に使わない。

count

NUL position / kind -> display group only

- Rust 型は info.rs に置き、C サイズ・定数との一致を host 試験で検査。装置 enum の Rust 複製は不要。

### lib

スクロールは追加しない。

表示幅52桁×8px=416px、左右PAD込み436pxで WIN_W_MAX=440 内。

WIN_H_MAX を368へ広げ

> [要確認: 計画の時制?]

- 高さは既存レイアウト計算を使い画面内へ clamp。

最長行、services の末尾、OK / Esc が両モードで見えて操作できること
