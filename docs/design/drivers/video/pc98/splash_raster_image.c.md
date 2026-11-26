# sys/platform/pc98/video/splash_raster_image.c — 既定のパレットでの約 2 秒の前置き

> 状態: **現行 (2026-11-18)** — 出所: RASTER_SPLASH_*_DESIGN・TASK_RASTER_SPLASH (要約して移した)
ソース: `sys/platform/pc98/video/splash_raster_image.c`・`sys/platform/pc98/video/splash_raster_image.h`

## 何をするか

`pl_preview(state)` は、画像を VRAM へ転送した後、行ごとのパレットの再生を始める前に、PC-98 の既定の 16 色のパレットで約 2 秒 (`SPLASH_RASTER_PREVIEW_MS` = 2000) 見せる。
これは演出 (ユーザー指定 2026-11-04)。VRAM のデータが既定のパレットで見えた後、行ごとの再生で突然カラフルになる。実機で最初に既定の 16 色が見えるのは崩れではない。
表示時間は「既定のパレットを倍に、ラスタを 3 倍に」(ユーザー 2026-11-04) で 2 秒と 6 秒になった (6 秒は `sys/include/platform_splash.h`)。

## 口の契約

- 前提は判定済み・dynamic_ok・入口 IF=1・周期が正の値。違えば -1。
- `palette_init()` で既定の色にし、新しい VBLANK に 1 回合わせてから、2 秒以上になる最小の完全なフレーム数を数える。最初の端数のフレームで前置きを短くしない。
- 回数は掛け算の比較のループで求め、64bit の割り算を使わない。待ちは IF=1 のままで、`PL_PREVIEW_POLL_LIMIT` (1,000,000 回) で止まって -1。
- 呼び手は転送の後に 1 回だけ呼ぶ。-1 なら黒に戻して NO_SYNC で終える。

## 診断だけに残る型

`PL_MAX_OUTS` (6) と `struct PlRow` は、診断の probe (`sys/platform/pc98/splash_probe.c`) の測定用の列だけが使う。通常の再生は [splash_raster_play.c](splash_raster_play.c.md) の `PlV2Row` を使う。

## 検査

- `test_splash_obs.py (旧版 8ab116a7a93e)`・`test_splash_wire.py`・`test_splash_v2.py`: 前置きが 1 回だけ呼ばれ、経過が期限から差し引かれること。前置きの削除・無課金は実行時 RED。
- `test_splash_raster_diag.py (旧版 c907f6a84336)`: `PlRow` を使う診断の経路。
