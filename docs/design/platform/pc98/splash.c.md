# sys/platform/pc98/splash.c — スプラッシュ用の表示の開始・描画・歩調

> 状態: **現行 (2026-11-18)** — 出所: RASTER_SPLASH_*_DESIGN・TASK_RASTER_SPLASH (要約して移した)
ソース: `sys/platform/pc98/splash.c`

## 何をするか

- `platform_splash_begin()`: 表示の希望 (GFX=) を保存し、PC98 の標準表示を選んで `gfx_init()` し、希望を元に戻す。BB が planar の 4 プレーンで取れなければ `gfx_shutdown()` して -1。最後に `pc98_splash_raster_state()` を呼び、走査の判定 (8 周期) を済ませておく。
- `platform_splash_clear(color)`・`platform_splash_fill_rect(...)`: 借りた BB への塗り (クリップあり)。
- `platform_splash_clear_pages()`: 表示ページを両方消す (`gfx_clear_planar_pages`)。走査の判定の状態の寿命は変えない。
- `platform_splash_wait_frame(end_tick)`: `sys_time_now` で 1 フレームの周期 (判定前なら 24k の 400 行で代用) を待つ。end_tick に達したら 0 で早く戻る。時計の失敗・逆行・`SPLASH_WAIT_POLLS` (1,000,000 回) の到達は -1。入口の IF を変えない (IF=0 でも有限で終わる)。

## 境目

- 起動のときは必ずある PC98 標準表示だけを使う。任意の装置の probe と init は後の通常の `gfx_init` に任せる。
- 画像の読み込み・再生はしない ([splash_image.c](splash_image.c.md))。

## 落とし穴

- `platform_splash_wait_frame` の周期の計算は 64bit の割り算 (`__udivdi3`) を使う。libgcc の依存を外す段の対象。
- 診断の像では `gfx_init()` の前に `platform_splash_probe_before()` を呼び、09A8h を begin の前後で比べる。

## 検査

`test_boot_splash_native.py (旧版 729852acc8b2)`・`test_dep_dir_d_splash.py`・`test_splash_wire.py` がこのファイルを組む。
