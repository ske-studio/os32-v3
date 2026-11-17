# drivers/video/geforce/geforce_body.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `drivers/video/geforce/geforce_body.c`

起動中、割り込み有効化とスプラッシュの後に GFX が GeForce の場合だけ、GUI=0 の起動でも取得を 1 回試み、その 1 回は割り込みを約 2ms 停止する。
登録が失敗した時は lease を取得せず、取得後の loader 失敗では解放を試み、復元失敗時は lease を保持する。

本番 context/復帰 callback は起動中保持して module を pin し、正常終了後も予約は保持する。owner の核移設、共通装置 registry、N1 lease の再設計は行わない。
module 初期化は gf_production_prepare 成功後に platform の identify を結線し、登録成功後に kernel が wc_start を 1 回呼ぶ。BAR・台帳・DISPLAY surface・PTE は起動予約済み。WC=1/UC=0 は後の描画開始へ進み、復元未確認の負値は active lease に保持して後の wc_start で描画開始を失敗させる。

診断の常駐 ROM/plan/lease は別の起動で既存寿命を維持する。

GF_ACCEL S12: 共通 stop は identify() で identity を確認し、通常は WC lease を起動中保持したまま gf_production_quiesce を呼ぶ。identity 喪失時だけ wc_stop→gf_production_quiesce の順で CPU 側を戻す (装置アクセスは行わない)。WC 復元失敗でも quiesce を通して失敗を返す。HAL の停止と kernel の geforce_return (abort/回収) が同じ callback を使う。
