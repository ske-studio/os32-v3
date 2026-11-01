# drivers/video/geforce/gf_present.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `drivers/video/geforce/gf_present.c`

LFB は 01/02、銀行窓 4F05 p.46 は不使用
`drivers/video/pc98/backend_pegc.c` の HAL 契約を手本
present は矩形・pitch に従う CPU 転送、行間の中断点、read-safe な GPU 読取りによる posted write 排出を保つ（`docs/design/kdebug/gfinit_plan_c.md`）。毎フレームの VRAM 読戻しはせず、present_bytes/commits を更新し hw_ops=0。

各行は宛先の実アドレスを 4 バイト境界に揃えるまで byte、続いて dword、残りを byte で転送する。RAM の非整列入力は 4 バイトの `__builtin_memcpy` で読み、矩形外にはアクセスしない。詳細は [高速化の確定設計](../../../../decisions/realhw/GF_ACCEL_DESIGN.md) §2。

受入版だけ `GF_PRESENT_BYTE` で byte store を選ぶ。旗は build/geforce-module.mk の
受入対象だけが渡す。既定の dword の .text は切替追加前と同じであることを比較する。
