# sys/platform/pc98/video/splash_raster.h — 走査の判定の状態と内部の口

> 状態: **現行 (2026-11-18)** — 出所: RASTER_SPLASH_*_DESIGN・TASK_RASTER_SPLASH (要約して移した)
ソース: `sys/platform/pc98/video/splash_raster.h`

## 何をするか

PC-98 標準の 16 色の画面で、行ごとにパレットを書き換える処理 (起動のロゴ・公開 KAPI `gfx_present_raster`・起動の画像・ビューアー) が共有する「走査の判定の状態」と、その周期の定数を持つ。

## 周期の定数 (Subsystem 層)

行の周期は µs の有理数 `SPLASH_RASTER_LINE_NUM_US / den` で表す (分子 100000)。値は `sys/platform/pc98/pegc.h` の 400 行の SYNC の表 (`PEGC_GDC_MSYNC_400`・`PEGC_GDC_MSYNC_400_31K`) から実行時に取り出す。

| 周波数 | den | 1 行 | 総行数 | VS | VBP | 1 フレーム |
|---|---|---|---|---|---|---|
| 24kHz | 2483 | 約 40.27µs | 440 | 8 | 25 | 約 17.72ms |
| 31kHz | 3147 | 約 31.78µs | 449 | 2 | 34 | 約 14.27ms |

- 24k の値は NEC PC-9800 Technical Databook Hardware (1993) p.351 の表と一致する。31k の表はこの資料に無いので、SYNC の値と実測で決めた。
- `SPLASH_RASTER_SYNC_TIMEOUT_US` (100ms) は VSYNC の待ちの上限。`MIN_GAP` 2 は公開 table の異なる行の間隔の下限。
- `*_BLANK_NUM_US` (24k は 9.88µs、31k は SYNC から求めた H×20/100) は参考値で、実測の bit6 の幅とは別物。

## 状態 `struct SplashRaster`

- mode (24K/31K)・line_num_us/line_den・total_lines/vs_lines/vbp_lines・active0_lines・high_us・checked・dynamic_ok・last_result・frame/late/dropped/unapplied。
- active0_lines は、VSYNC の立下り F の後で可視行 0 の前にある行数。bit5 (VSYNC) の高の期間が「非表示全体」型なら 0、「VS だけ」型なら VBP。NP21/W は前者で、実機の型は実測で確かめる。
- dynamic_ok=1 は、周期の照合と型の分類が両方通ったという意味。0 でも begin は成功で、固定色の表示はできる。

## 寿命と口の契約

- 状態の実体は `splash_raster.c` の static 1 つ。`pc98_splash_raster_state()` は初回の利用で `begin` を呼び (遅延判定)、以後は同じ状態を貸す。
- `pc98_splash_raster_reset()` は、gfx の初期化の入口 (`sys/hal/gfx/gfx_core_init.inc`) と `gfx_shutdown` (`sys/hal/gfx/gfx_core.c`) から `display_provider_raster_reset` (`sys/platform/pc98/gfx_selector_lifecycle.inc`) 経由で呼ばれる。判定の結果を次の gfx_init へ持ち越さない。
- `begin`・`frame`・`table` は `SPLASH_RASTER_OK` (0) か `SPLASH_RASTER_NO_SYNC` (-1) を返し、last_result にも残す。
- 行ごとの画像の再生 (`pl_v2_*`) は別のファイル ([splash_raster_play.c](splash_raster_play.c.md)) で、この状態を借りるだけ。

## 診断だけの型

- `SPLASH_RASTER_DIAG` の内側の型 (`SplashRasterProbeCost`・`ProbeEntry`・`EventDiag`・`Diag`) と `pc98_splash_raster_diag()` は診断の像だけにある。記録は次の frame/reset まで借りる。
- `SplashRasterBudget`・`SplashRasterEntryBudget`・`SplashRasterImageBudget`・`enum SplashRasterWave`・`SPLASH_RASTER_IMAGE_OUTS` (5)・`IMAGE_DIAG_OUTS` (6) は、いまは診断の probe (`sys/platform/pc98/splash_probe.c`) だけが使う。通常の再生は使わない。
- 予算の式の意味 (ヘッダのコメント): Q はサンプル間隔 (IN と比較と計数を含む)、D は成功した IN から列の開始まで、W は列全体、B は bit6 の高の幅、E は誤差。採否は `Q+D+W+E < B_min` と `Q < min(B_min, H_min−B_max)`。単位は 1/den µs の整数比。
- `SPLASH_RASTER_PER_LINE` は使う所が無い。
