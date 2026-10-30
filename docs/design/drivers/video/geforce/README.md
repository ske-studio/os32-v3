# drivers/video/geforce/ — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `drivers/video/geforce/`

GUI/グラフィックコンソール→RAM CLIENT→`gfx_present/dirty`→`backend_geforce`→video service→GF 部品。KAPI とアプリ側描画経路を変えない（[HW1]、`O/include/gfx_hal.h:37–97`）。

配置は **`geforce.mod`＋核の薄い予約/復帰口**（U5）。下層/中層は `hwdev/gf/`、HAL のバックエンドは `drivers/video/geforce/` (共通層は `gfx/`、TASK_SRC_LAYOUT L2 で移った)、診断 adapter は `kdebug/` と従来 hwdev.mod。本番は KDEBUG=0 で動く。
## 索引

- [hwdev/gf/](../../../hwdev/gf/README.md)
- [include/video_service.h](../../../include/video_service.md)
