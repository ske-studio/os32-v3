# hwdev/gf/display/palette.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/display/palette.c`

| 機能・ファイル語幹 | 中身・初版の境界 | nouveau（`N/` 以下） | VBE 対応 |
|---|---|---|---|
| パレット `palette` | mask/index/RGB、部分更新、256 項の shadow | `dispnv04/hw.c:795–810`、`crtc.c:778` | 4F09、p.54 |
palette の first/count と FB の offset/pitch/height を既存範囲内で検査し、不正なら I/O 前に返す。
移動した各機能の既存試験、AR/DAC/LIVE/FP/PLL復元・独立期待列・CHECK

## 索引

- [hwdev/gf/display/](README.md)
