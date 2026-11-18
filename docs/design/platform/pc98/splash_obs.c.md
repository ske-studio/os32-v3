# platform/pc98/splash_obs.c — 観測の像: 16 枚の画像を順に再生して記録する

> 状態: **現行 (2026-11-18)** — 出所: RASTER_SPLASH_*_DESIGN・TASK_RASTER_SPLASH (要約して移した)
ソース: `platform/pc98/splash_obs.c` (画像は `tools/raster_obs_assets.py`・`tools/img2osri.py` で作る)

## 何をするか

1 行で書き換える色の数や OUT の本数を変えた画像を実機で再生し、カメラとシリアルで崩れを見るための専用の像。通常の像には入らない。
`make kernel-splash-obs` が `SPLASH_OBS_BUILD=1` で別の BUILD_OUT に像・map・対の部品を作る。診断の像 (`SPLASH_DIAG_BUILD`) と同時には指定できない (`build/select.mk` が止める)。

## 流れ

- `platform_splash_observe()` は UART を初期化し、`/boot/obs/N01.osri`〜`N16.osri` を順に試す (8.3 の名前)。置くファイルを選べば 1 枚だけ試せる。
- 各枚: TVRAM の左上に `N=07` などを出す → `SPLO n=.. start crc=..` → 読み込み → `pl_v2_prepare` → `SPLO n=.. out_max=.. out_total=..` → 黒で転送 → `pl_preview` (約 2 秒) → 5 秒ぶんのフレーム数 (`SPLASH_OBS_SECONDS`、周期から切り上げ) だけ `pl_v2_frame` → 黒 → 解放 → `SPLO n=.. end status=.. frames=.. drop=..`。
- status は missing・no_sync・framebuffer・memory・invalid・prepare・ok・drop (1 フレームも OK が無く DROP だけ)。失敗しても次の枚へ進む。
- 再生器と準備器は通常の像と同じ関数 (`pl_v2_prepare`・`pl_v2_frame`) で、観測専用の型や上限は無い。

## 使い方の注意

- 走査の間はログを出さない。実機ではノートがシリアルに受信時刻を付け、カメラの写真 (約 240ms 間隔) と時刻で突き合わせる。判別できないものは「未確認」と記録する。
- 停止して end の行が出なければ、ノートの watchdog (16 × (5 秒 + 読み込み) + 起動、カメラの保持 300 秒未満) で止まったと記録する。

## 検査

`tools/tests/test_splash_obs.py` (splash-obs-host): 実 C の走査と持ち主の処理を I/O の模型で動かし、通常と観測で同じ列になることを確かめる。
