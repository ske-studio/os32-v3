# drivers/video/cirrus/backend_cirrus.c — Cirrus 画面バックエンド

> 状態: **現行 (2026-10-31)**

Cirrus のモード、リレー、描画面と表示を HAL に提供する。

## 落とし穴

- **写像とリレーの寿命**: shutdown でページ写像を剥がすと、次のアプリの初期化前に必要な面を使えず fault しうる。
  起動時の supervisor + PCD 写像を保ち、CPL=3 には CLIENT 面だけを lease する (`drivers/video/cirrus/backend_cirrus.c:303`)。
  再 init はリレー状態を 0 に戻し enter で再設定し、shutdown はハード側の窓を閉じる (`drivers/video/cirrus/backend_cirrus.c:565`)。
  `tools/tests/test_gfx_kernel_fb.py` が面の寿命を検査する。
- **窓と probe**: RAM の上端だけでは穴にある窓を誤拒否する。窓全体のページを物理地図に問い合わせる (`drivers/video/cirrus/cirrus_identify.c:9`)。
  リニア窓はデバイス aperture に置き (`include/wab_xe10.h:183`)、auto probe は NP21/W 検出後にボード ID を読む (`drivers/video/cirrus/backend_cirrus.c:217`)。
  `tools/tests/test_cirrus_win.py` が窓の重複・範囲を検査する。

## 設計の補足

| バックエンド | 画素形式 | VRAM | NP21/W で検証 |
|---|---|---|---|
| ウィンドウアクセラレータ (Cirrus GD54xx) | 8bpp 以上、チップの BitBLT | 独自 VRAM 1〜4MB。VRAM 窓 / MMIO 窓は 0xF00000〜0xF9FFFF に開く | **可** (§4)。他チップ (S3 / MGA / Trident) は不可 |

| 観点 | Cirrus 内蔵 (Xe10 等) | Cirrus C バス製ボード |
|---|---|---|
| present 1 ピクセルあたりの VRAM 書き込み | 1 バイト (CPU 転送時) | 1 バイト |

Cirrus:

- アクセラレータの価値は「CPU がピクセルを運ばない」こと。主記憶のバックバッファで合成して
  present する方式だとエンジンが遊び、C バス製ボードでは CPU→VRAM 転送が PEGC 並みかそれ
  以下になる。**画面の実体をカードの VRAM に置き、塗り・矩形移動・文字の色展開をエンジンで
  行う** (B1 改訂の理由)。

  閾値は `backend_cirrus.c` の `CIRRUS_CPU_DIRECT_PIXELS` (256 画素)。

/* Cirrus も既存 WIDTH/HEIGHT/PITCH を宣言時だけ使う 480 行を 1 件。DAC=6。 */

- 宣言以外の clip・転送 pitch・clear 範囲と query の幅は gfx_backend_mode() から得る。

- Cirrus は表示 offset=0、CLIENT=0x4B000、pattern=0x96000 を維持。各面 300 KiB、主記憶 BB を新設しない。
