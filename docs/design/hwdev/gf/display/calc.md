# hwdev/gf/display/calc.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/display/calc.c`

値計算の再実装はしない。modeset の順番・待ち・条件は `gf_load_state` だけが所有する。
mode_bit と zero 初期化は共有、集約関数は呼出しだけ。
移動した各機能の既存試験、AR/DAC/LIVE/FP/PLL復元・独立期待列・CHECK

## 索引

- [hwdev/gf/display/](README.md)
