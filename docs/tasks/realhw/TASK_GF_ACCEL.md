# TASK_GF_ACCEL — GeForce 6200 の画面の転送を速くする (段 0: dword の present + MTRR の WC)

> 上位: [実機の票の索引](PLAN.md)

> 状態: **受入待ち (2026-11-02)** — 段 0 は main。Ra266 の全面 present p95 は byte UC 50.4ms → dword WC 7.37ms。**「足りる」の基準はユーザーが決めていない** (16.7ms・33ms は PM が選択肢の説明と設計に書いた案で、PM が達成と書いたのは誤り — ユーザー指摘 2026-11-02「1フレームに収まれば十分と私は回答したのでしょうか？」)。典型操作の台本の計測も未実施。M2MF に進むかは実測をユーザーに示して決める。

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
| 段 0 | dword present + MTRR WC + 計測 | **済み** — main (段 0a 3eb3c958・MTRR 11103b61・結線 f5468c6c・受入の計測 5fae6183)、NP21/W guest_tests 21/21。**Ra266 (c916ba78 の手元ビルド、2026-11-01、ノート、証拠 ノートの `~/os32-tmp/evidence/2026-11-01/realhw-c916ba78-gfa/`)**: 全面 640×480 の p50/p95 = byte UC 50424/50431µs、dword UC 15669/15670µs、**dword WC 7370/7372µs**。320×200: 11360/11377 → 4138/4154 → 2378/2401。640×16: 1687/1704 → 531/547 → 255/260。32×32・16×16・1×480・1×1 はほぼ同じ (行ごとの処理が主)。MTRR: BIOS の枠 0 を残し、空いていた枠 2 に 0x30000000/512KiB の WC (mask 0xffff80800)。PTE 75 頁は不変。PAIR 0xcd9ad898、leftover=0。未確認: 0..255 の模様のカメラ (フレームが 5 分で消えた)、GUI 終了時の MTRR の復元 (ログに終了後の段が無い、ホスト試験は見ている)、GUI の操作の台本 (立会い) |
| 後続 | M2MF (段 0 で足りないときだけ) | **ユーザー判断待ち** — 「足りる」の基準はユーザーが決める (2026-11-02、PM が自分の目標値で「作らない」と書いたのを撤回) |
