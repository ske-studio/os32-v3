# hwdev/gf/display/pll_program.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/display/pll_program.c`

実 ROM の解は N1/M1=26/7、N2=M2=1、P=2、actual=25,071 kHz。要求値との差 104 kHz、約 0.41%。36 MHz 診断値を使わない（`D/GF6200_4C_DESIGN.md:181`）。
| 機能・ファイル語幹 | 中身・初版の境界 | nouveau（`N/` 以下） | VBE 対応 |
|---|---|---|---|
| PLL `pll_program` | solver と 5 書込み、選択 50C・524 | `nvkm/subdev/devinit/nv04.c:200`、`dispnv04/hw.c:468` | 4F0B p.59／02 の内部 |
