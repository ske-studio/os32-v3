# hwdev/gf/display/start.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/display/start.c`

| 機能・ファイル語幹 | 中身・初版の境界 | nouveau（`N/` 以下） | VBE 対応 |
|---|---|---|---|
| 表示先頭 `start` | START=0、単位は BAR1 相対 byte | `dispnv04/crtc.c:838–908`、`hw.c:772` | 4F07、p.50 |

## 移し替えた設計

600800=0
