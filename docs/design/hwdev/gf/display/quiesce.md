# hwdev/gf/display/quiesce.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/display/quiesce.c`

| 機能・ファイル語幹 | 中身・初版の境界 | nouveau（`N/` 以下） | VBE 対応 |
|---|---|---|---|
| 停止 `quiesce` | 1850→1854→PMC140 を 0、旧 timing は戻さない | 完全対応無し、PC-98 共存上の差分 | 04 の完全復元ではない |
stop は局所後始末→quiesce、続けて上層が 98 切替を行う。
停止済み stop・同値 blank は成功。
終了/障害/reboot 前は **局所 index/lock/窓復元→1850=0→1854=0→PMC140=0→route00→decode 安全確認→98 復元・予約セル往復・再描画**（`D/GF_STAGES_DESIGN.md:115–117`）。
移動した各機能の既存試験、AR/DAC/LIVE/FP/PLL復元・独立期待列・CHECK
