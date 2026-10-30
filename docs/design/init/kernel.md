# init/kernel.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `init/kernel.c`

**`GFX=geforce` は `GUI≠1` のとき許す**（F5・N4）。起動予約前に既存の設定読取りで判定し、GUI=1 なら拒否して pc98 を選ぶ。GUI 省略時は既定値 0（`O/init/kernel.c:521`）。最初の `gfx_init`（GUI または CUI の描画アプリ）で、承認待ち無しに devinit を一度行う（N5、`O/gfx/gfx_core.c:960,1014`）。
