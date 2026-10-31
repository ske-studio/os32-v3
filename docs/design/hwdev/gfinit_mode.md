# hwdev/gfinit_mode.h — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gfinit_mode.h`

表示は **640×480、PACKED8、256 色、pitch=640、表示先頭=0、25.175 MHz 要求**の 1 モードとする。

## 移し替えた設計

推奨は head A、ANALOG OR0、640×480 progressive・8bpp indexed・pitch 640 B、25,175 kHz、負H/負V、約31.469 kHz/59.94 Hz。H=640/656/752/800、V=480/490/492/525（表示/同期開始/終了/総数）。
