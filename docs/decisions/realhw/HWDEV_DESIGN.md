# 実機の調査・試行・復帰の核と装置部品 — 設計 r2 (構造)

> 状態: **現行 (2026-10-31)** — 現行の決定だけを残した (設計は docs/design/、経緯はアーカイブの元の文書)

- **推奨: A (C と表の装置部品化) を先行。HW-BASIC は GeForce 実機合格後に同じ核へ接続する。**
- 対象は置き場・境界・寿命。確定済みの戻す処理と段 4a の文書 ([REALHW_RECOVER_DESIGN.md](REALHW_RECOVER_DESIGN.md)・追記 2 本・[GF6200_4A_DESIGN.md](GF6200_4A_DESIGN.md)・[gfinit_decode.md](../../design/kdebug/gfinit_decode.md)) の許可・保存・復帰・N1・承認を変更しない。
- 単一利用者 / 単一タスク / 協調切替、P100・32MB、整数のみ。CRC は破損・取り違え検出であって権限の根拠ではない。

## 6. GeForce 4a S1〜S6 の移し方と S7 以降

- N1・CR1F・0FACh・CUI/31kHz・S0 と S1 の区別などの安全条件は [GF6200_4A_DESIGN.md](GF6200_4A_DESIGN.md) と [REALHW_RECOVER_DESIGN.md](REALHW_RECOVER_DESIGN.md) のまま維持する (この分離で変えない)。GF 部品には 0FACh 操作を渡さない。
