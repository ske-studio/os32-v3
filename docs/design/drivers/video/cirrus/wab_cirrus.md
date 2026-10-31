# drivers/video/cirrus/wab_cirrus.c — 設計
> 状態: **現行 (2026-10-31)** — 出所: DESIGN (D1 で移した)
ソース: `drivers/video/cirrus/wab_cirrus.c`

- **チップドライバ**はレジスタ体系・BitBLT・リニア FB を扱い、PC-98 のポート番号を
  一切含まない。VGA レジスタは 3C0h 系の論理番号で `glue->out(reg, val)` に渡す。

- **小さい操作は設定コストが本体を上回る**: BitBLT 1 回にレジスタ書き込み 10 本前後。十数
  ピクセル角以下は CPU 直書きの方が速い。GD5430 の BLT レジスタは MMIO 化できるので、
  OUT ではなくメモリ書き込みで設定する。**逸脱 (2026-09-06、H3 実装)**: Xe10 内蔵モードには
  MMIO 窓が無い (NP21/W `wab/cirrus_vga.c` の Xe10 経路は 0FAAh/0FABh + VGA レジスタ写像のみ)
  ため、現行の `drivers/video/cirrus/wab_cirrus.c` は BLT レジスタを **I/O (GR 拡張レジスタ) 経由**で設定する。

- **エンジンは非同期**: 次の描画や CPU の VRAM アクセスの前にビジービットを IN で待つ。
  細かい操作の連発は同期待ちが支配的になるので、操作を矩形単位にまとめて投げる。
