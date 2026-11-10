# kdebug/gfinit_resources/bars.inc — 設計
> 状態: **現行 (2026-11-10)**

`pci_bar_get` の記録から長さを取得し、元の BAR 値との一致を照合する。
未割当ては省略し、長さ 0・不整列・4 GiB 範囲外・件数超過を拒否する。
64bit の上位も照合し、表現可能な区間を exclude に追加する。
詳細は [資源収集設計](../gfinit_resources.md)。
