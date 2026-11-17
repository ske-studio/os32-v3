# platform/pc98/gfx_init.c — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_T2D_T2H (表示不具合の修正) の契約を今のソースと照らして要約
ソース: `platform/pc98/gfx_init.c`

PC-98 標準 planar 表示の GDC 操作と、両ページの消去。

## 両ページの消去 (`gfx_clear_planar_pages(plane_size)`)

- 描画ページ 0 を選んで B/R/G/I の 4 面をそれぞれ先頭から `plane_size` バイト 0 にし、描画ページ 1 で同じことをする。戻りは描画ページ 1。CPU の直書きで、GRCG・EGC の描画コマンドは使わない [HW1]。
- 量は 400 行で 32,000 B (`GFX_PLANE_SZ`)、200 行で 16,000 B (`GFX_PLANE_SZ_200`)。どちらのページも同じ量で、面の一部・片方のページだけを消さない。
- 呼ぶ所: スプラッシュの片付け (`platform_splash_clear_pages` が 400 行の量で)、`gfx_init`・`gfx_init_200` の共通部 (`gfx/gfx_core_init.inc`)。スプラッシュのロゴや前の GUI の像が裏ページに残らないようにする。

## 検査

`tools/tests/test_display_cleanup.py` (スプラッシュの 4 つの希望値と失敗後の再試行、両ページの全面を汚してからの 400/200 行の初期化) と `tools/tests/test_boot_splash_native.py`。どの面・どのページの消去を省いても、その面・ページの残像として実行時に検出する。
