# hwdev/gf/display/output.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/display/output.c`

| 機能・ファイル語幹 | 中身・初版の境界 | nouveau（`N/` 以下） | VBE 対応 |
|---|---|---|---|
| GF 出力選択 `output` | head A/OR0、DACCLK・TEST、他経路を停止 | `dispnv04/dac.c:375–409`、`dfp.c:93–110` | Core の独立機能無し |

## 索引

- [hwdev/gf/display/](README.md)
