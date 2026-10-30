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
