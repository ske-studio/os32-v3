# タスク04: スクロール最適化 (差分スクロールエンジン)

作成日: 2026-04-24

---

## 1. 目的

フルフレーム再描画 (全576タイル) から差分更新へ移行することで、
30fps スクロールを実現する。

PC-98のVRAMバス帯域制限 (16bit bus @ 8MHz相当) により、
フルフレーム更新は ~365ms/frame (3fps相当) かかる。

---

## 2. 実装フェーズ

### Phase 1 — 列単位ダーティ追跡 ✅ 完了

**概要**: 全画面 `gfx_add_dirty_rect(384x384)` を廃止し、
描画した列のみダーティ登録する。

**実装ファイル**:
- `programs/libtilemap/tilemap_compose.c`
  - `drawn_tracking_reset()` / `drawn_tracking_mark()` / `flush_drawn_dirty()`
  - `tilemap_present()` を `gfx_present_dirty()` に変更

**ベンチマーク結果** (`tile_bench` x30, 2-Layer Mixed, Delta 1-tile move):
| 方式 | ticks | ms |
|------|-------|----|
| compose_btf | 26 ticks | 260ms |
| compose_btf_fast | 30 ticks | 300ms |
| compose_ftb | 36 ticks | 360ms |

---

### Phase 2 — 差分水平スクロール ✅ 完了

**概要**: タイルマップのスクロール量から dx を算出し、
バックバッファ(BB)を `memmove` でシフトして露出列のみ再描画する。

**実装ファイル**:
- `programs/libtilemap/tilemap_compose.c`
  - `bb_shift_horiz(shift_bytes)` — BB水平バイトシフト
  - `draw_column_btf(screen_col, map_col)` — 露出列の再描画
  - `tilemap_compose_scroll()` — 差分スクロールの統合エントリ
- `programs/libtilemap/tilemap_bg.c`
  - `tilemap_scroll_sync()` — prev_scroll 状態の明示的リセット

**ベンチマーク結果** (`tile_bench v`, Scroll x10, H-diff):
| 方式 | ticks | ms/frame |
|------|-------|----------|
| btf (full redraw) | 365 ticks | 365ms |
| compose_scroll H-diff | 77 ticks | 77ms |

→ **約4.7倍高速化** (full redraw 比)

---

### Phase 3 — 差分垂直スクロール ✅ 完了

**概要**: 垂直方向の差分更新 (`bb_shift_vert`) を実装。
 Triple Fault は `make clean` 漏れによる構造体サイズの不整合(`gfx_fb` の破壊)が原因であったため修正済み。また、差分更新時の VRAM 同期漏れを防ぐため、各描画関数に `drawn_tracking_mark()` を追加。

**実装ファイル**:
- `programs/libtilemap/tilemap_compose.c`
  - `bb_shift_vert(shift_lines)` — BB垂直ラインシフト
  - `draw_row_btf(screen_row, map_row)` — 露出行の再描画

---

## 3. バグ調査記録 — V-diff Triple Fault (解決済み)

### 3.1 根本原因

`make clean` 漏れによる **構造体サイズの ABI 不整合** が原因。

具体的には `GFX_Framebuffer` のレイアウト変更後、`libtilemap.a` だけ再ビルドされず、
古い `gfx_fb` オフセットを参照したためポインタが破壊された。
`make clean && make all` で解消。

### 3.2 デバッグ経緯

| 症状 | 観察値 |
|------|--------|
| `p0` | 0 (NULLポインタ) |
| `pitch` | 毎フレーム +16 ずつ累積 |
| クラッシュ位置 | `bb_shift_vert` 内の `memcpy` |

### 3.3 副次修正

差分更新時のVRAM同期漏れを防ぐため、露出タイルの各描画関数に
`drawn_tracking_mark()` を追加:

- `draw_column_btf()` → `drawn_tracking_mark(screen_col, row)`
- `draw_row_btf()` → `drawn_tracking_mark(col, screen_row)`
- `redraw_upper_bgs()` → `drawn_tracking_mark(col, row)`

---

## 4. 参考: ベンチマーク全体結果

`tile_bench` (全テスト, 2026-04-24 時点)

### Full Draw
| シナリオ | compose_btf | compose_btf_fast | compose_ftb |
|---------|-------------|-----------------|-------------|
| 1-Layer Opaque x5 | ~133 ticks | ~134 ticks | ~153 ticks |
| 4-Layer Heavy x3  | ~125 ticks | ~99 ticks | ~130 ticks |

### Delta Update (1-tile move x30, 2-Layer Mixed)
| 方式 | ticks |
|------|-------|
| compose_btf | 26 ticks |
| compose_btf_fast | 30 ticks |
| compose_ftb | 36 ticks |

### Flip (Full Draw x3)
| 方式 | ticks |
|------|-------|
| No flip | 95 ticks |
| H-flip / V-flip / HV-flip | ~138-139 ticks |

### Scroll (x10)
| 方式 | ticks | ms/frame |
|------|-------|----------|
| btf (full redraw) | 365 ticks | 365ms |
| compose_scroll H-diff | 77 ticks | **77ms** |
| compose_scroll V-diff | 計測予定 | — |

---

## 5. 今後の最適化候補

詳細は [05_SCROLL_ASM_OPT.md](05_SCROLL_ASM_OPT.md) を参照。

- **Phase 4**: NASM による `bb_shift_horiz` / `bb_shift_vert` 高速化 (`rep movsd`)
- **Phase 5**: `draw_column_btf` / `draw_row_btf` のタイルブリット NASM 化
- **Phase 6**: H-diff + V-diff 同時処理 (斜めスクロール対応)
