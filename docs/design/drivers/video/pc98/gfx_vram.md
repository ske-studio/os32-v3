# sys/platform/pc98/video/gfx_vram.c — dirty 転送とラスタ提示

> 状態: **現行 (2026-11-01)**。

VRAM の転送・dirty と prev_dirty の管理・表示ページの flip はこの部品が持つ。
`gfx_present_raster` は count を検査し、packed 系は通常の dirty present へ渡す。
planar は dirty を一括転送する。flip 時は今フレームの dirty を保存して
前フレームの dirty をマージ・転送し、保存したキューを prev_dirty にして flip する。
non-flip は初回も一括転送し、パレット更新と行ごとの転送を交互に行わない。
planar の commits は dirty の有無によらず呼び出し 1 回につき 1、
present_bytes は `_flush_dirty_queue` で実転送量を数える。

転送後は `pc98_splash_raster_state()` と `pc98_splash_raster_table()` に渡す。
共通再生状態・24/31kHz の遅延判定・絶対可視行・HBLANK 待ち・IRQ 窓・
同期失敗の有限待ちは再生器が持つ。line=0 は初期色、段 0 は各行 1 件・
行間隔 2 以上で、既存ロゴは 4 行 1 件を保つ。120µs の相対待ちは使わない。
入口 IF=0・判定不可・同期失敗・予算違反でも転送済み像は提示し、void で戻る。

`gfx_init` / `gfx_init_200` の共通入口と権限検査を通った `gfx_shutdown` で
共通再生状態をリセットする。splash の外で公開 KAPI を呼んでも同じ状態を借り、
次回の初期化へ測定結果を持ち越さない。

## 検証と落とし穴

`test_boot_splash_native.py` は実際の core / VRAM / 再生器と偽の有理数走査を組み、
両周波数・flip 有無・初回 dirty / ステイル転送 / 転送なし・不等間隔の行・
同期失敗 / IF=0 / 予算違反・再初期化を確認する。各変更の省略と120µs復活は
コンパイル成功後の実行時 RED で検出する。実測の行位置・3 秒間の commits 差は
NP21/W、実機の位相と I/O 予算は PM の受入で確認する。

## KAPI 引数の範囲

> 出所: TASK_T2D_T2H (T2e の KAPI の範囲検査の欠落の修正) の契約を今のソースと照らして要約 (2026-11-18)

- `gfx_add_dirty_rect` は登録の前に `gfx_clip_screen` で画面 (幅 640・現在の高さ) へ切り、画面の外なら登録しない。巨大な矩形も画面の中に収まる。
- `gfx_present_raster` は表の `count` が 0 以下または `GFX_RASTER_MAX_ENTRIES` を超えれば転送も表の再生もせず、初回提示の診断だけ通って戻る (負の count で表の外を読まない)。

検査は `tools/tests/test_kapi_bounds.py`。
