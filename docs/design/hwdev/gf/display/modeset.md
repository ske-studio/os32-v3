# hwdev/gf/display/modeset.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/display/modeset.c`

| 機能・ファイル語幹 | 中身・初版の境界 | nouveau（`N/` 以下） | VBE 対応 |
|---|---|---|---|
| モード設定 `modeset` | 中層から共有列を呼ぶ組立て | `dispnv04/crtc.c:708,731`、`hw.c:827` | 4F02、p.40 |
PLL2/IO_RESTRICT_PLL2 と P3 PLL は列全体の成功時だけ解除し、正常 stop で完了済みPLLを戻さない。
B は `int gf_load_row(struct gf_display_context *, const struct gfinit_mode *, u32 ordinal)` を `gf_load_io.row` へ接続し、section/wait も既存型を使う（`O/hwdev/gf_load_state.h:36–41`）。

## 索引

- [hwdev/gf/display/](README.md)
