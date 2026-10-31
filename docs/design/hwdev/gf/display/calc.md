# hwdev/gf/display/calc.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/display/calc.c`

値計算の再実装はしない。modeset の順番・待ち・条件は `gf_load_state` だけが所有する。
mode_bit と zero 初期化は共有、集約関数は呼出しだけ。
移動した各機能の既存試験、AR/DAC/LIVE/FP/PLL復元・独立期待列・CHECK

## 移し替えた設計

推奨は head A、ANALOG OR0、640×480 progressive・8bpp indexed・pitch 640 B、25,175 kHz、負H/負V、約31.469 kHz/59.94 Hz。H=640/656/752/800、V=480/490/492/525（表示/同期開始/終了/総数）。

CR05=((HBE>>5)&1)<<7 | (HE&1F)=9F（`V/crtc.c:350–408`, `V/nvreg.h:197–198`, `V/hw.h:34`）。
