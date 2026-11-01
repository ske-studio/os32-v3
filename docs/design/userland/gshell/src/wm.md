# userland/gshell/src/wm.rs — 設計
> 状態: **現行 (2026-10-31)** — 出所: API_CONTRACTS (D1 で移した)
ソース: `userland/gshell/src/wm.rs`

- 全画面 present は WM だけが行う (デスクトップ切替、フルスクリーン GFX からの復帰)。

## システムパレット

gshell が起動時に `gfx_set_palette` でこの表を入れる

## パレットの退避・復元

フルスクリーン GFX プログラムの前後で WM が退避・復元する。

WM はその前後でパレット全体を退避・復元する。

- 所有者 = exec のネスト段 (`res_owner_get()`、FD と同じ)。アプリ終了 / kill 時に WM が
  そのアプリのウィンドウ・サーフェス・タイマを全部回収する。

## WM の全周期

**WM の全周期**: 入力取り込み → WM 自身の操作 (タイトルバーのドラッグ、閉じるボタン、メニュー、ダイアログ、フォーカス切替、パレットのリース切替) → クロームとデスクトップの損傷を present → カーソル → `sys_halt`。

- 移動・リサイズは WM が決め、結果は `Configure` イベントで返る (アプリ側で座標を持たない)。

- ウィンドウ / サーフェス / タイマ / ダイアログはアプリの所有。終了・kill で WM が回収 (T4)。

## GeForce の受入計時

`gf_accel_accept` featureだけがwm_cycleのイベント入口/更新完了とflush_presentを
計時する。gserialのpollは計時の前。presentを呼ばないidleは採らず、10更新のwarmup後
300更新をRAMへ保存し、全計時が終わってからGFW1を送る。イベント処理中に発生した
presentも別計上し、total=event+composite+presentを保つ。compositeは残りのRAM描画/
WM更新処理で、アプリが次の周期に描く時間やイベントを待つidle時間は含めない。
`wm_cycle` 外のCOMMITやPump→input→cursorも共通の`flush_present`を通る。
その転送は独立の更新として採り、total=present、event/composite=0とする。
周期内の転送は同じ周期に加算し、独立sampleを重ねない。warmupと300件の上限は共通。
時刻取得失敗は全内訳を0xffffffffにし、正常群に混ぜない。
`GF_ACCEPT_ID`と`GF_ACCEPT_MODE`をビルド環境に与える。通常buildには配列も時計もない。
同じ起動で1回だけ採るので、窓移動/スクロール/カーソルの各台本は別起動で記録する。
