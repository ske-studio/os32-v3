# HAL に織り込む描画の高速化 (DIRECT/SHADOW)

> 状態: **実装中 (2026-11-08)** — 設計は確定 ([GFX_ACCEL_HAL_DESIGN](../../decisions/gui/GFX_ACCEL_HAL_DESIGN.md))。段 31 本、第 1 波 (P0a・P0b・H1・H2a) から。

## 0. ユーザー決定 (2026-11-08、原文)
- 「すべて要ります。重要度別に分けて機能実装。あ、でもHALに織り込んだほうが良いのかも？」
- 「HALが実装された場合はネイティブやPEGCなど機能的にバッファリングした方が良いものが利用する方にします。つまり、十分に描画が早い場合はHALからのグラフィックへの書き込みのみに出来るようにする方を正にします。」
- 「cirrusは物理デバイスがないのでそのままでいいです。」
- 「GeforceはHALからVRAM」— GeForce の既定は DIRECT。計測は効果の記録で、切り替えの条件にしない。
- 将来の 3D・浮動小数点の口への申し送り: 「HALではCPUチェックを行いMMX Pentium以上から受け付けるのが現状良いと思います。それ以下は大きめのテーブルを用意して計算させるほうが実用的に思います。」

## 1. 要約
- 方式: 描画の速い backend (GeForce) は DIRECT — libos32gfx・libos32gui・WM が HAL の口 (KAPI の submit) から VRAM へ直接。Trident・PEGC・PC-98・Cirrus は SHADOW (RAM の CLIENT + present) のまま。
- 重要度の順: P0 (典型操作の計測) → D1 (GPU の fill と put) → D2 (窓の移動・スクロールを VRAM の中の複写) → D3 (SHADOW の変わった所だけ) → D4 (文字の mono) → D5 (M2MF)。
- 今回足す口は INDEX8 専用。16/32bpp の形式付きの口は別の版 (他社カードの調べ `~/os32-tmp/run/gpu-survey/SURVEY.md` §6 の 17 件と合わせる)。
- 段の表・並列の順・試験と変異は確定設計の §7・§8。

## 2. 段の状態
| 段 | 状態 |
|---|---|
| P0a・P0b・H1・H2a (第 1 波) | 未着手 |
| ほか 27 段 | 未着手 (確定設計 §7 の依存の順) |
