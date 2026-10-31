# userland/gshell/src/damage.rs — 設計
> 状態: **現行 (2026-10-31)** — 出所: API_CONTRACTS・DESIGN (D1 で移した)
ソース: `userland/gshell/src/damage.rs`

### G4. 無効化と commit (Wayland の damage + commit)

- `invalidate(window, rect)`: 損傷を申告する。WM が統合し (32px 境界、隣接は結合。既存
  `gfx_add_dirty_rect` と同じ規則)、次の `Paint` イベントを発行する。

- [Paint 配送と commit (handler.rs)](handler.md)

| 項目 | 値 |
|---|---|
| 損傷矩形 | 最大 8 件 / ウィンドウ (32px 境界、隣接は結合) |

## 1. 再描画モデル (採用済み 4 点)

**InvalidateRect 方式**。アプリは汚れた矩形を申告し

Win3.1 が GUI の骨格をこの形にしたのは 386 の VRAM 帯域のため
