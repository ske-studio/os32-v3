# TASK_GF_ACCEL — GeForce 6200 の画面の転送を速くする (段 0: dword の present + MTRR の WC)

> 上位: [実機の票の索引](PLAN.md)

> 状態: **設計中 (2026-11-01)** — 設計は確定 ([GF_ACCEL_DESIGN](../../decisions/realhw/GF_ACCEL_DESIGN.md)、astra 起草 r1〜r3、Opus レビュー 2 回)。段 0 の実装はこれから。

## 0. ユーザーの決定 (2026-11-01)

- 「現在のタスク後に GeForce の加速の設計を行います」。
- Opus のレビューで、今の present が 1 byte ずつの UC の store で、32bit の store + VRAM の write-combining (MTRR) だけで GPU の DMA 転送 (M2MF) より速くなる見込みと分かった。選択肢の原文: **「段0を先に、足りれば終了 (Recommended)」** — present を 32bit 書き込み + MTRR WC にして実機で測り、目標を満たせば GPU の加速は作らない。満たさないときだけ M2MF に進む。

## 1. 詳細設計 (確定) の要約

- 今の描画は RAM の backbuffer に CPU が描き、present が VRAM へ写す。HAL の `gfx_hw_fill_rect`・`gfx_hw_blit` は GUI から呼ばれない — fill/copy の加速だけでは速くならない。
- 段 0a: `drivers/video/geforce/gf_present.c` を dword の store に (行の端の byte は別、矩形の外は読まず書かない)。
- 段 0b: BAR1 の表示域 512KiB を可変 MTRR の WC に (SDM §11.11.7–8 の順、BIOS の MTRR を壊さない選び方、空きが無い・重なるなら適用しない)。PTE は今のまま (PCD=1・PWT=0 → MTRR WC で実効 WC)。WC の吐き出しは present の後の BAR0 の read。GUI の終了・CUI・回収で元に戻す。
- 計測: 実機 Ra266 で byte UC / dword UC / dword WC の 3 方式、全面と典型の矩形の p50/p95 をシリアルで。表示はカメラ (RA266 §3-1)。M2MF は CPU の最良の全面 p95 の 0.8 倍以下のときだけ。
- 段の表 (S0〜S16・T0〜T3、15 段) と受入・変異は確定設計の §7・§8。

## 2. 段の状態

| 段 | 中身 | 状態 |
|---|---|---|
| 段 0 | dword present + MTRR WC + 計測 | 計画 |
| 後続 | M2MF (段 0 で目標に届かないときだけ) | 計画 |
