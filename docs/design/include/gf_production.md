# include/gf_production.h — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `include/gf_production.h`

`GFX=geforce` は `GUI≠1` のとき許し、auto の候補にはしない（U2 決定）。
**`GFX=geforce` は `GUI≠1` のとき許す**（F5・N4）。起動予約前に既存の設定読取りで判定し、GUI=1 なら拒否して pc98 を選ぶ。GUI 省略時は既定値 0（`O/init/kernel.c:521`）。最初の `gfx_init`（GUI または CUI の描画アプリ）で、承認待ち無しに devinit を一度行う（N5、`O/gfx/gfx_core.c:960,1014`）。
**診断 owner/lease は `kdebug/gfinit.c:70–74` のまま**（F7）。核から `int gf_production_selected(void)` 一つを export し、GFX=geforce の起動では gfinit の入口で拒否する。本番の成否や GUI 終了後にも拒否を保ち、診断と本番は設定変更＋再起動で入れ替える。S0/緊急復帰入口は止めない。
