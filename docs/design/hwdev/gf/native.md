# hwdev/gf/native.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/native.c`

BAR 全 decode 範囲を台帳予約し、BAR0 の必要頁と BAR1 の表示面を既存 master PD の supervisor+PCD で写像する。予約凍結後に拡張せず、実使用前は ID/base/属性の一致を読取りで確認する。
不一致・未知 ID・範囲不成立では GF を使わず pc98 を維持する。BAR1 の窓長と実 VRAM 容量は別で、既存 PFB 容量確認・表示面範囲も維持する。変換は `P2V_IO` と既存写像 API（[C5]、`O/gfx/gfx_core.c:305–327`）。
本番全体のソフト上限は既存 `GF_INIT_EXEC_US` の 60 秒を採用（`O/kdebug/gfinit_exec.h:4`）。期限超過は既存失敗出口へ進むが、戻らない MMIO に壁時計上限は保証できない。

## 索引

- [hwdev/gf/](README.md)
