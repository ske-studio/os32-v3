# hwdev/gf/display/timing.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/display/timing.c`

| 機能・ファイル語幹 | 中身・初版の境界 | nouveau（`N/` 以下） | VBE 対応 |
|---|---|---|---|
| timing `timing` | H/V→標準 CR・overflow、固定 progressive | `dispnv04/crtc.c:237–413` | 02 の内部 |

## 移し替えた設計

timing・SR/GR/AR・pitchをstate化

CR00..18=`5F 4F 4F 83 53 9F 0B 3E 00 40 00 00 00 00 00 00 E9 2B DF 50 00 DF 0C 43 FF`

CR05=((HBE>>5)&1)<<7 | (HE&1F)=9F（`V/crtc.c:350–408`, `V/nvreg.h:197–198`, `V/hw.h:34`）。
