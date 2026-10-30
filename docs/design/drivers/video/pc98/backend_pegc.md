# drivers/video/pc98/backend_pegc.c — PEGC 画面バックエンド

> 状態: **現行 (2026-10-31)**

PEGC のモード設定、画面と表示を HAL に提供する。

## 落とし穴

- **実機 ROM とモード列**: BIOS の残り状態やエミュレータの値に頼ると、実機で 640×480 が崩れうる。
  `drivers/video/pc98/backend_pegc.c:320` の GDC とクロックの列を実機 ROM の OUT 列と比較する。
  `tools/tests/pegc_mode_host.c:381` の除外表には A0h DFh / A2h 28h の未解明の差がある。`make check-pegc-mode-host` と実機表示の両方で確かめる。
- **窓の RAM 判定**: 高位 RAM があっても低位に穴があるため、RAM 上端で窓を拒否しない。
  `drivers/video/pc98/backend_pegc.c:568` は対象範囲を `pgalloc_range_has_ram` に問い合わせる。`tools/tests/test_pgalloc_range.py` が範囲判定を検査する。
