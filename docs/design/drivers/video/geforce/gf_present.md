# drivers/video/geforce/gf_present.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `drivers/video/geforce/gf_present.c`

LFB は 01/02、銀行窓 4F05 p.46 は不使用
`drivers/video/pc98/backend_pegc.c` の HAL 契約を手本
present は矩形・pitch に従う CPU 転送、行間の中断点、read-safe な GPU 読取りによる posted write 排出を保つ（`D/GF6200_4C_DESIGN.md:161`）。毎フレームの VRAM 読戻しはせず、present_bytes/commits を更新し hw_ops=0。
