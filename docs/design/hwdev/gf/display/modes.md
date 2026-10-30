# hwdev/gf/display/modes.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/display/modes.c`

初版は独立 DPMS・非零の表示先頭・HW cursor を提供せず、DPMS は modeset 内の OFF/ON だけ（U4 決定）。
| 機能・ファイル語幹 | 中身・初版の境界 | nouveau（`N/` 以下） | VBE 対応 |
|---|---|---|---|
| 情報・モード一覧 `modes` | 既知装置と 1 モード、現在モードを返す | `dispnv04/crtc.c:643`（設定側。OS32 の一覧そのものは無し） | 4F00/01/03、pp.25/30/44 |
start-offset は BAR1 相対 byte、0 だけ成功、非零は NOSYS。
mode ID と配列 index は区別する。
情報取得は I/O・割当て・再初期化をせず、失敗時 out をゼロにする。

## 索引

- [hwdev/gf/display/](README.md)
