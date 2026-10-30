# drivers/video/geforce/geforce_platform.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `drivers/video/geforce/geforce_platform.c`

BAR 全 decode 範囲を台帳予約し、BAR0 の必要頁と BAR1 の表示面を既存 master PD の supervisor+PCD で写像する。予約凍結後に拡張せず、実使用前は ID/base/属性の一致を読取りで確認する。
