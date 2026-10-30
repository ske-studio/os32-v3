# kdebug/gfinit_exec.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `kdebug/gfinit_exec.c`

context は head/skip、有限の呼出し/反復 stack、ROM 借用参照、既存上限・時計・`gf_io`。bus は read/write（BAR/offset/width/値）、wait_us、point、局所復元。診断の記録・承認は adapter が担当する。
診断の段別承認/記録/中断/S0 は adapter に残し、C plan・A1/A4/VPLL/virtual が使う汎用機能は維持する。
旧 plan executor の丸ごと移植や恒久的な二重実装はしない。
保存した差分 trace/制御 case/診断段別承認・記録・中断/S0/C-only退行で統一を確認。

## 索引

- [hwdev/gf/](../hwdev/gf/README.md)
