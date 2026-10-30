# kdebug/gfinit_mode.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `kdebug/gfinit_mode.c`

mode_bit と zero 初期化は共有、集約関数は呼出しだけ。palette:112–123 と模様 row:124–141 は診断に残す。
