# hwdev/gf/display/baseline.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/display/baseline.c`

| 機能・ファイル語幹 | 中身・初版の境界 | nouveau（`N/` 以下） | VBE 対応 |
|---|---|---|---|
| 基準状態 `baseline` | init 後に一度採取、再 modeset の入力 | `dispnv04/hw.c:813`（採る範囲は OS32 限定） | 4F04 p.45 の一部分 |
基準値は main4 後に CR1C/33/3C/4B/52・60081C/600850/680524、初回 C040、PMC_ENABLE を一度だけ採る。全部成功した時だけ valid、CR52=(初回+4)&FF を維持する（`D/GF_STAGES_DESIGN.md:67–74`）。

## 索引

- [hwdev/gf/display/](README.md)
