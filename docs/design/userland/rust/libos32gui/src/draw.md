# userland/rust/libos32gui/src/draw.rs — GUI 描画プリミティブ

> 状態: **現行 (2026-10-31)**

描画対象の clip を守り、矩形や字形を描く。

## 落とし穴

- **広い矩形の画素単位処理**: 画素ごとに境界を何度も比較すると、大きな塗りで present 前に長く止まる。
  `userland/rust/libos32gui/src/draw.rs:134` は先に矩形を交差し、行ごとに `write_bytes` で塗る (`:179`)。
  遅延時は描画前後の `gfx_counters` と実行位置を対象ビルドの map と照合し、起床待ちと描画時間を区別する。

## 設計 (D1 で移した)

> 出所: API_CONTRACTS・DESIGN

### G2. プリミティブ (すべて `surface: SurfaceId` を第 1 引数に取る)

| 関数 | 内容 |
|---|---|
| `fill_rect(rect, style)` | 塗り。`bg` を使う |
| `draw_rect(rect, style)` | 1px 枠。`fg` |
| `hline / vline / line(x0,y0,x1,y1, style)` | 線。`fg` |
| `blit(dst_xy, bitmap, src_rect)` | ビットマップ転送 (カラーキー = 色 255) |
| `text(x, y, utf8: &[u8], style)` | UTF-8 を字形 engine (欧文 Inconsolata・和文 JF ドット東雲16、欠字と失敗は KCG) の 1bpp で描く (T7b-b4)。半角 8px / 全角 16px の cell、baseline は face ごと (欧文 13・和文 14、KCG は cell の先頭行から)、cell の外は描かない。clip は画素単位 (全角・オフスクリーンも)。0x5C は ROM の ¥ |
| `measure_text(utf8) -> (w, h)` | レイアウト用 |

- 対応する HAL のプリミティブ: 塗り / 矩形転送 / 文字 / ビットマップ (DESIGN §7-1)。
  線と枠はクライアント側の CPU 実装 (HAL には要求しない)。

| 採用 | 解く欠点 | 386 でのコスト |
|---|---|---|
| **ステートレス描画** (呼び出しごとに Style を渡す) | SelectObject の順序問題 | なし |
