# hwdev/gf_load_state.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf_load_state.c`

**決定**：本番 modeset は nouveau の load_state に近い簡素な作りとし、試験 C と `gf_load_state`・生成列を同一ソースで使う（`D/GF_STAGES_DESIGN.md:59–63`）。
初版は独立 DPMS・非零の表示先頭・HW cursor を提供せず、DPMS は modeset 内の OFF/ON だけ（U4 決定）。
| 機能・ファイル語幹 | 中身・初版の境界 | nouveau（`N/` 以下） | VBE 対応 |
|---|---|---|---|
| DPMS `dpms` | SR01→10ms→CR17→CR1A、modeset の OFF/ON は必須 | `dispnv04/crtc.c:175–234`、`dac.c:460` | 拡張 4F10、p.63 |
B は `int gf_load_row(struct gf_display_context *, const struct gfinit_mode *, u32 ordinal)` を `gf_load_io.row` へ接続し、section/wait も既存型を使う（`O/hwdev/gf_load_state.h:36–41`）。
分割/複製無し。唯一の生成元 `tools/gfinit_mode_tables.py:29` を維持。blank/DPMS の modeset 順序もここに保持。

## 索引

- [hwdev/gf/](gf/README.md)
