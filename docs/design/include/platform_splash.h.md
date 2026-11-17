# include/platform_splash.h — 起動のスプラッシュの入口と時間

> 状態: **現行 (2026-11-18)** — 出所: RASTER_SPLASH_*_DESIGN・TASK_RASTER_SPLASH (要約して移した)
ソース: `include/platform_splash.h`

## 何をするか

起動の前景 (同期の呼び出しだけ) で使うスプラッシュの口と、表示時間の定数を決める。公開 KAPI ではなく、型・番号を変えない。
走査の判定の状態はここで持たず、`drivers/video/pc98/splash_raster.h` の `pc98_splash_raster_state()` から借りる。

## 時間の定数

| 定数 | 値 | 意味 |
|---|---|---|
| `PLATFORM_SPLASH_TICKS` | 3 秒 | 内蔵のロゴの表示 |
| `PLATFORM_SPLASH_IMAGE_PREVIEW_SECONDS` | 2 | 画像の前置き (既定のパレット、演出) |
| `PLATFORM_SPLASH_IMAGE_PLAY_SECONDS` | 6 | 画像の行ごとの再生 |
| `PLATFORM_SPLASH_IMAGE_TICKS` | (2+6) 秒 | 画像の経路の入口からの枠。読み込み・準備・転送・前置き・再生を全部含む |

前置きの 2000ms の実体は `SPLASH_RASTER_PREVIEW_MS` (`drivers/video/pc98/splash_raster_image.h`)。2 秒の値を変えるときは両方をそろえる。

## 口の契約

- `platform_splash_begin()`: 描画できれば 0。失敗のときは gfx を閉じ終えている。BB の記述は begin ごとに取り直し、成功した begin から `gfx_shutdown` までだけ描いてよい。
- `platform_splash_clear`・`fill_rect`・`clear_pages`: 借りた BB への描画と、表示ページ両方の消去。
- `platform_splash_try_image(path, &end_tick)`: 入力は起動の入口の期限、出力は「今の tick + 使わなかった tick」。結果は `PLATFORM_SPLASH_DONE` (0)・`FALLBACK` (1)・`NO_SYNC` (2)。動的な再生のモデルの時間で PIT の時計は進めない。
- `platform_splash_wait_frame(end_tick)`: 静止のロゴ・固定画像の、時計で 1 フレームぶん待つ歩調。使える時計が無ければ非 0。
- `SPLASH_RASTER_OBS` の像だけ `platform_splash_observe()`、`SPLASH_RASTER_DIAG` の像だけ `platform_splash_probe_before()`・`platform_splash_probe()` がある。

実体は [platform/pc98/splash.c](../platform/pc98/splash.c.md)・[splash_image.c](../platform/pc98/splash_image.c.md)・[splash_obs.c](../platform/pc98/splash_obs.c.md)・[splash_probe.c](../platform/pc98/splash_probe.c.md)。
