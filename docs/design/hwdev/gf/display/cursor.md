# hwdev/gf/display/cursor.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/display/cursor.c`

カーソルは初版では GF ハードカーソルを非表示にするだけで、GUI のソフトウェア描画を使う。
| 機能・ファイル語幹 | 中身・初版の境界 | nouveau（`N/` 以下） | VBE 対応 |
|---|---|---|---|
| cursor `cursor` | offset=0・非表示だけ | `dispnv04/cursor.c:9–59` | Core に該当無し |
