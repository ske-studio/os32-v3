# drivers/video/cirrus/backend_cirrus.c — Cirrus 画面バックエンド

> 状態: **現行 (2026-10-31)**

Cirrus のモード、リレー、描画面と表示を HAL に提供する。

## 落とし穴

- **写像とリレーの寿命**: shutdown でページ写像を剥がすと、次のアプリの初期化前に必要な面を使えず fault しうる。
  起動時の supervisor + PCD 写像を保ち、CPL=3 には CLIENT 面だけを lease する (`drivers/video/cirrus/backend_cirrus.c`)。
  再 init はリレー状態を 0 に戻し enter で再設定し、shutdown はハード側の窓を閉じる (`drivers/video/cirrus/backend_cirrus.c`)。
  `tools/tests/test_gfx_kernel_fb.py` が面の寿命を検査する。
- **窓と probe**: RAM の上端だけでは穴にある窓を誤拒否する。窓全体のページを物理地図に問い合わせる (`drivers/video/cirrus/cirrus_identify.c`)。
  リニア窓はデバイス aperture に置き (`include/wab_xe10.h`)、auto probe は NP21/W 検出後にボード ID を読む (`drivers/video/cirrus/backend_cirrus.c`)。
  `tools/tests/test_cirrus_win.py` が窓の重複・範囲を検査する。

## probe の段 (`cirrus_probe`)

出所: TASK_S5 (§6 の R2 の観測) の契約を今のソースと照らして要約 (2026-11-18)。

1. 窓を張れるか (`cirrus_identify`)。窓が張れないならボードの ID が合っても使えないので、これを最初に見る。RAM に当たる構成では NP21/W の判定も ID も読まない。
2. ボードの ID を読みにいってよいか。auto では NP21/W の上に居るとき (`np2_detect` = I/O 07EFh の `NP2` 応答) だけ進む。`GFX=cirrus` の明示時は判定せずに進む。実機 (PCI の Trident) で 0FAAh・0FABh を叩かない代わりに、auto の probe 1 回ごとに 07EFh の検出通信 (OUT 3 回 + IN 最大 7 回) が 1 組増える。
3. ボードグルーの ID 判定 (9801 や WAB の無い機種はここで落ちる)。
4. チップの解錠キーの往復。

結果は `s_probed`・`s_probe_ok` に 1 回だけ残す。NP21/W の Cirrus 構成 (`USEGD5430`) の auto では `hal_test` が `backend cirrus (packed 8bpp)` で、実行中のリニア窓 `s_lin` は 0xFE000000 (2MB)。
`tools/tests/test_cirrus_win.py` の段 7 と `--mutate` (auto で門を通らずに ID を読む・`GFX=cirrus` でも門を課す・窓の判定より前に NP21/W を判定する) がこの順序を固定する。

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
