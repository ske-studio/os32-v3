# hwdev/gf/rom.inc — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/rom.inc`

PROM は BAR0 から既存の二重読み/header/checksum/既知 script 検査で採取する。x86 は解析せず、全段 hash 固定も追加しない。未知 ROM/命令・処理失敗は再試行せず S0 相当へ進む。
共有 parser は一つ。診断用出力/接続・lifetime/stages・owner/lease は元の場所に残す。

## 索引

- [hwdev/gf/](README.md)
