# gfx/gfx_core_init.inc — 400/200 行初期化の共通部

> 状態: **現行 (2026-11-01)**。

`gfx_core.c` の `gfx_init()` / `gfx_init_200()` は同じ翻訳単位の
`gfx_init_mode(legacy200)` をそれぞれ 0 / 1 で呼ぶ。
入口で共通ラスタ再生器の状態をリセットし、次の table 利用で周期・位相を再判定する。
世代を進めてバックエンドを選択し、既存の revoke → bind → 世代更新を通る。
packed 系は backend の init / enter に任せ、200 行要求でも native 解像度を保つ。
既存の失敗出口と診断の時機を保つ。

planar は表示高さを選択モードから得る。消去量は各面 400 行で 32,000 bytes、
200 行で 16,000 bytes とし、BB と VRAM 両ページへ同じ量を使う。
CLIENT / DISPLAY の登録 geometry と 32,000 bytes 刻みの面間隔は変更しない。
GDC のモード設定・START、CPU による両ページ消去、ページ選択、palette、
scroll、世代更新、enter、診断の順を保つ。描画コマンドは使わない [HW1]。

## 落とし穴

`gfx_display_page` と `prev_dirty.count` のリセット位置は入口で異なる。
400 行では bind 前、200 行ではページ選択・flip 有効化後に行う。
CLIENT が無い場合の早期 return でもこの違いを保つ。
200 行で BB / VRAM の後半まで消したり、面間隔を 16,000 bytes に詰めたりしない。

実ソースを使う `test_gfx_reinit.py` の geometry / clear-range 変異、
`test_gfx_kernel_fb.py` の 200 行 bind 変異、`test_display_cleanup.py` の
両入口の VRAM 消去省略変異を維持する。展開後のソースまたはこの断片の一意な
目印を置換し、コンパイル成功後の期待した実行時失敗で確認する。
