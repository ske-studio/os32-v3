# タスク06: libtilemap リファクタリング計画

> 発行: PM (2026-04-24) / 状態: **受入完了 (2026-04-24)**

## 目的の要約

Phase 4〜5.5b の性能最適化フェーズを通じて、`libtilemap` のコードベースには以下の問題が蓄積された。

1. **重複したタイル描画ループ** — `tilemap_compose_btf`, `tilemap_compose_btf_fast`, `redraw_upper_bgs_partial`, `tilemap_compose_scroll` 内のダーティセル処理の内側にそれぞれ独立したBG走査ループが存在し、同一のパターン（attr取得 → tile取得 → prepare_tile_surface → gfx_blit）が少なくとも4か所に重複している。

2. **責務の混在** — `tilemap_compose_scroll` は1関数で差分計算・BBシフト・露出タイル再描画・上層BG再描画・通常ダーティ処理・scroll更新を全て行っており、800行超のファイルの中で最も肥大化した関数になっている。

3. **可読性の低下** — 最適化時に追加された `gfx_dirty_suppress` フラグのセット/クリアが複数パスに散在している。`can_fast` 判定とNASMパスの分岐が `draw_column_btf`/`draw_row_btf` で重複している。

本ドキュメントは **実装前の調査フェーズ** として、調査すべき観点とファイルを列挙するものである。

---

## 1. 調査すべきファイル

| ファイル | 規模 | 役割 |
|---------|------|------|
| `programs/libtilemap/tilemap_compose.c` | 890行 | 全合成パス・スクロール処理の実装本体 |
| `programs/libtilemap/tilemap_bg.c` | 82行 | BG面の初期化・スクロール設定API |
| `programs/libtilemap/tilemap_core.c` | 156行 | tilemap_init / tilemap_free / タイル登録 |
| `programs/libtilemap/libtilemap.h` | 92行 | 公開API定義・TileDef構造体 |
| `programs/libtilemap/tilemap_internal.h` | 20行 | モジュール内部共有構造体 |
| `programs/libtilemap/asm_tilemap.asm` | 922行 | NASM高速パス全実装 |
| `programs/tests/tile_bench.c` | — | ベンチマーク・動作テスト |
| `programs/tests/demo_tile.c` | — | 動作デモ・目視確認 |

---

## 2. 調査すべき重複・冗長箇所

### 2-A. タイル1枚の描画パターンが4箇所に重複

以下のパターンが `tilemap_compose.c` 内で繰り返されている。

```c
attr = _tilemap.bg_planes[bg].map[row][col];
tile_id = TILEMAP_ID(attr);
tile = &_tilemap.tiles[tile_id];
if (tile->opacity == TILE_TRANSPARENT) continue;
dx = _tilemap.origin_x + col * TILE_W;
dy = _tilemap.origin_y + row * TILE_H;
prepare_tile_surface(&tmp_surf, tile, attr, flip_buf);
if (tile->opacity == TILE_OPAQUE) gfx_blit(...);
else                               gfx_blit_transparent(...);
drawn_tracking_mark(col, row);
```

**出現箇所:**
- `tilemap_compose_btf` (L256〜291)
- `draw_column_btf` の fallback パス (L520〜549)
- `redraw_upper_bgs_partial` (L634〜662)
- `tilemap_compose_scroll` 内の dirty セル処理 (L838〜866)

**調査すべき点:**
- `draw_one_tile_btf(int col, int row, int bg, GFX_Surface *surf, u8 flip_buf[][])` のような共通ヘルパーに集約できるか
- `calc_tile_draw` (スクロール・クリップ考慮版) と scroll不使用版 (単純 `origin + col*TILE_W`) の使い分けが統一されているか確認

---

### 2-B. `draw_column_btf` と `draw_row_btf` の構造的重複

- L482〜551 (`draw_column_btf`) と L557〜625 (`draw_row_btf`) は `can_fast` 判定 → NASMパス → fallbackパスの構造が完全に同一
- NASMパスのポインタ配列構築・`asm_draw_column_tiles` / `asm_draw_row_tiles` 呼び出しが並列で存在

**調査すべき点:**
- 2関数のdiff量を確認し、共通部分をマクロまたは関数に抽出できるか
- NASMパスの `can_fast` 判定ロジック（BG0 visible + 全行 opaque + フリップなし）が両関数で同一かどうかを精査

---

### 2-C. `gfx_dirty_suppress` の散在

`tilemap_compose_scroll` 内で `gfx_dirty_suppress = 1` / `= 0` が設定されているが、`tilemap_compose_btf` でも同様に設定されている。

**調査すべき点:**
- `gfx_dirty_suppress` を操作しているすべての箇所を grep で列挙
- スコープが明確でないため、クラッシュや不正なダーティ状態になるリスクがあるか確認
- `tilemap_compose_scroll` のフォールバックパス (`tilemap_compose_btf` 呼び出し) で suppress が二重にセットされないか確認 (L735)

---

### 2-D. `force_all_dirty_internal` の必要性

- L667〜677: BG全面 dirty 化。`tilemap_compose_scroll` のフォールバック時のみ使用
- 同様の処理が `tilemap_bg.c` の `tilemap_bg_set_scroll` 等にも存在する可能性

**調査すべき点:**
- `force_all_dirty_internal` と公開API `tilemap_force_redraw`（もしあれば）の重複確認
- `tilemap_bg.c` 内の dirty 操作との関係

---

### 2-E. `tilemap_compose_btf_fast` の存在意義

- L380〜444: `tilemap_compose_btf_fast` は `tilemap_compose_btf` の2タイルペア版
- 現在 `tile_bench` や `demo_tile` から直接呼ばれているか不明

**調査すべき点:**
- `tile_bench.c` / `demo_tile.c` での呼び出し箇所を確認
- `tilemap_compose_ftb` (FtB) との使い分けが文書化されているか
- 使われていない場合はデッドコードとして削除候補

---

### 2-F. `drawn_tracking_reset` / `drawn_tracking_mark` の呼び出しパターン

- `tilemap_compose_scroll` では差分描画とdirtyセル処理の両方で `drawn_tracking_mark` を呼ぶが、`drawn_tracking_reset` は一度しか呼ばれていない
- BBシフト保持領域のダーティマーク (L810〜821) が行端のみ (row=0, row=ROWS-1) を対象にしているのは意図的か

**調査すべき点:**
- ダーティマーク追跡ロジックの正確な意味論を文書化
- BBシフト後の保持領域が「列全体」ではなく「行端のみ」でも `flush_drawn_dirty` が正しく動作することを確認

---

## 3. 可読性の問題点

### 3-A. `tilemap_compose_scroll` の肥大化

- L679〜880: 1関数で約200行。以下の論理ブロックが直列に並んでいる:
  1. スクロール差分計算
  2. フォールバック判定
  3. 水平差分 (BBシフト + 露出列描画 + 上層BG)
  4. 垂直差分 (BBシフト + 露出行描画 + 上層BG)
  5. 通常ダーティ処理
  6. `flush_drawn_dirty` / prev_scroll 更新

- 関数分割またはコメント区切りの強化で構造を明示化できる

### 3-B. C89制約による変数宣言の見通しの悪さ

- C89ルールのためブロック先頭宣言が必須だが、スコープが広い変数と局所的な変数が混在し、どの変数がどの処理で使われるか追いにくい
- 特に `tilemap_compose_scroll` の `int bg, dx_total, dy_total, need_full_redraw, has_upper_bg` は全体で使われる一方、ブロック内の `int shift_bytes, abs_bytes, cols_exposed, start_col, c` は局所的

### 3-C. マジックナンバー

- `L816`: `drawn_tracking_mark(c2, 0)` / `drawn_tracking_mark(c2, TILEMAP_ROWS - 1)` — 行端のみをマークする意図がコメントなしでは不明
- `L722`: `(TILEMAP_COLS * TILE_W) / 2` — ジャンプ閾値の根拠が不明

---

## 4. 調査の優先順位

| 優先度 | 項目 | 理由 |
|--------|------|------|
| 高 | 2-A: タイル描画パターンの共通化 | 最も重複が多く、バグ修正時の影響範囲が広い |
| 高 | 2-C: `gfx_dirty_suppress` の散在 | 潜在的バグリスク |
| 中 | 2-B: column/row 描画関数の統合 | コード量削減、保守性向上 |
| 中 | 3-A: scroll関数の分割 | 可読性の大幅改善 |
| 低 | 2-E: `btf_fast` の死活確認 | デッドコード削除 |
| 低 | 2-F: drawn_tracking 意味論 | バグではないが文書化不足 |

---

## 5. 実装方針 (調査後に確定)

本ドキュメントは調査フェーズのアウトラインであり、実装方針は調査結果をもとに別途確定する。
実装時は以下の制約を遵守すること:

- **C89 (GNU89) 必須**: `//` コメント禁止、ブロック先頭変数宣言
- **ベンチマーク回帰禁止**: リファクタリング前後で `tile_bench v` の結果が悪化しないこと
- **1コミット1変更**: 関数の抽出・移動ごとにマイクロコミット

---

*作成: 2026-04-24*
