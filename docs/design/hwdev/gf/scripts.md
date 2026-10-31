# hwdev/gf/scripts.h — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/scripts.h`

context は head/skip、有限の呼出し/反復 stack、ROM 借用参照、既存上限・時計・`gf_io`。bus は read/write（BAR/offset/width/値）、wait_us、point、局所復元。診断の記録・承認は adapter が担当する。

## 移し替えた設計

| D1 | `nvbios/init_scripts.txt` の全9本を集計すると193命令・27種（圧縮されたNV_REGは各4命令に展開） |
