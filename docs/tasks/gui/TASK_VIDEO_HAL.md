# TASK_VIDEO_HAL — 表示の HAL の設計 (GeForce と Trident のドライバの後)

> 状態: **計画 (2026-11-06)** — GeForce と Trident のドライバが出来上がってから設計に入る。v3 の柱 P9「Video HAL の共通化」(V3_PLAN §3) の票。

## 0. ユーザー決定 (2026-11-06、原文)

> GeforceとTridentのドライバが出来上がったらHALの設計に入ります。
> 最大機能をGeforceと取り、最低を640x400にとりあえずとります。
> 最低を640x400に取る理由は移植先の解像度がこれを下回る可能性があるからです。
> 解像度が下回るといって処理速度が劣るとは限らないので現状の最低ととるという意味です。
> そのうえで必要なHALの機能一覧を作成します。

| 項目 | 決定 |
|---|---|
| 着手の時期 | GeForce (票 TASK_GF_FEATURES) と Trident (票 TASK_TRIDENT_DRIVER) のドライバが出来上がった後 |
| 機能の上限 | GeForce が持つ機能を最大とする |
| 解像度の下限 | **640×400** (とりあえず)。今の最低の解像度。移植先の機械の解像度はこれを下回るかもしれないが、解像度が低いことは処理が遅いことを意味しないので、下限は「今の最低」として置く |
| 色の幅 | 下限は今の KAPI のパレットの口の **4 ビット/成分** (PC-98 の 16 色)、上限は **32 ビット** (GeForce、成分ごとに 8 ビット) (ユーザー 2026-11-06「最低が4bitなだけで最大32bitはいるはず」)。バックエンドは HAL の成分の幅を定数で受けて自分の DAC の幅へ換算し、4 ビットを決め打ちしない |
| 最初の成果物 | 上限 (GeForce) と下限 (640×400) の間で、HAL に要る機能の一覧 |

## 1. 材料 (設計に入るときに読む)

- 今の HAL: `include/gfx_hal.h`、設計記録 [gfx_hal.md](../../design/include/gfx_hal.md) (再描画モデル、HAL とバックエンドの表)。
- バックエンド: PC-98 (`drivers/video/pc98/`)、Cirrus (`drivers/video/cirrus/`)、GeForce (`drivers/video/geforce/`)、Trident (段 3、確定設計 [TRIDENT_S3_DESIGN](../../decisions/realhw/TRIDENT_S3_DESIGN.md))。
- 上に載るもの: P8 の GUI 層 (TASK_P8_GUI_LAYER)、移植の候補 (PORT_CANDIDATES、SDL 1.2・VESA2 的な互換層)。
