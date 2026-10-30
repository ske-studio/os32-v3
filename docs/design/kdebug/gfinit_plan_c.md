# kdebug/gfinit_plan_c.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `kdebug/gfinit_plan_c.c`

record でも ID/BAR/legacy/範囲/route/セル/バス失敗は止める。必須 CHECK と任意観測を区別し、全レジスタ readback を追加しない。
診断の node 生成接続、446 の呼出し、rows/build/control/vram/color は元の場所に残す（N3）。

## 索引

- [hwdev/gf/](../hwdev/gf/README.md)
