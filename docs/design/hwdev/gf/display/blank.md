# hwdev/gf/display/blank.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/display/blank.c`

| 機能・ファイル語幹 | 中身・初版の境界 | nouveau（`N/` 以下） | VBE 対応 |
|---|---|---|---|
| protect/blank `blank` | VGA protect と画面 blank、同期停止とは別 | `dispnv04/hw.c:110–124`、`hw.h:243`、`crtc.c:721` | Core の独立機能無し |
移動した各機能の既存試験、AR/DAC/LIVE/FP/PLL復元・独立期待列・CHECK
