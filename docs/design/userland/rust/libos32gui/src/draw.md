# userland/rust/libos32gui/src/draw.rs — GUI 描画プリミティブ

> 状態: **現行 (2026-10-31)**

描画対象の clip を守り、矩形や字形を描く。

## 落とし穴

- **広い矩形の画素単位処理**: 画素ごとに境界を何度も比較すると、大きな塗りで present 前に長く止まる。
  `userland/rust/libos32gui/src/draw.rs:134` は先に矩形を交差し、行ごとに `write_bytes` で塗る (`:179`)。
  遅延時は描画前後の `gfx_counters` と実行位置を対象ビルドの map と照合し、起床待ちと描画時間を区別する。
