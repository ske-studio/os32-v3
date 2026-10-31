# hwdev/gf/display/format.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/display/format.c`

| 機能・ファイル語幹 | 中身・初版の境界 | nouveau（`N/` 以下） | VBE 対応 |
|---|---|---|---|
| 画素形式 `format` | PACKED8・GR/AR の値（GENERAL は dac_format） | `dispnv04/crtc.c:418–449,589` | 01/02 の内部 |

## 移し替えた設計

MISC→SR0..4

C03C2=E3；SR00..04=00 21 0F 00 0E

GR00..08=`00 00 00 00 00 40 05 0F FF`；AR00..0F=00..0F、AR10..14=`01 00 0F 00 00`
