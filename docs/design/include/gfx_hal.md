# include/gfx_hal.h — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `include/gfx_hal.h`

GUI/グラフィックコンソール→RAM CLIENT→`gfx_present/dirty`→`backend_geforce`→video service→GF 部品。KAPI とアプリ側描画経路を変えない（[HW1]、`O/include/gfx_hal.h:37–97`）。
