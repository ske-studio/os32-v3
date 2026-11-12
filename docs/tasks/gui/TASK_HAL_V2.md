# 表示 HAL v2 (次の版の HAL の口)

> 状態: **実装中 (2026-11-11)** — 設計は確定 (2026-11-10 に Fable 5.2 のレビューを反映、11-11 に実装の欠けを追記) ([HAL_V2_DESIGN](../../decisions/gui/HAL_V2_DESIGN.md))。段は A0 → 実装の段 (並列) → I1・I2・I3。

## 0. ユーザー決定 (原文)
- 2026-11-08 3D・浮動小数点: 「HALではCPUチェックを行いMMX Pentium以上から受け付けるのが現状良いと思います。それ以下は大きめのテーブルを用意して計算させるほうが実用的に思います。」
- 2026-11-08 中核: 「現存するのが、実質Nvidiaとati（amd）なのでそこがコアである」、付録のカード: 「足りないものはソフトウェアで補って一応動作できる程度に出来れば良いです。アプリが重いなど、速度不足になるのは仕方ありません」
- 2026-11-09 色の形式: 「8bppはPEGCの限界です。限界は32bppは必要です。また、透過色などの余地としてu32 x 2は用意しておくべきでは？」
- 2026-11-09 2D の口: 「同様にGeforce6200の最大値とその世代の要件を満たすべきです。」
- 2026-11-09 能力の問い合わせ: 「設計をする必要があります。基本的にDirectX9cあたりまでの機能と抽象化層を用意します。」
- 2026-11-09 完了待ち: 「CPU が VRAM に触る前 (読み出しや、CPU での直接の書き込み) には、必要な fence まで待つ決まりにします。」→「これ」
- 2026-11-09 GPU から見える RAM・VRAM の面の確保: 「実装します。」
- 2026-11-09 画面と出力端子: 「DirectXなどが持っている情報粒度は持つべき」
- 2026-11-09 CPU 直描きの同期バックエンド層: 「HALにつなぐ口の前後に再度抽象化する層を設けることは出来ませんか？ポーリングしているタイプのビデオデバイス用の同期バックエンドを用意するということです。ここを可変で設定できるようにしておけば良いような？デバイス固有の数字なので別管理のコンフィグファイルなどは必要かもしれませんが。」→「10.も設計」
- 2026-11-09 PEGC: 「PEGCは現状のバックバッファを維持します。」
- 2026-11-09 設計の往復: 第 4 回まで「続ける」。2 枚同時の出力: 「AとC」 — A は adapter を全部列挙し同時に机を出すのは 1 adapter (その 2 heads は使える)、C は ACTIVE でない側 (PC-98 の内蔵) の TVRAM にコンソールを出し続ける。

## 1. 要約
- 全文は確定設計。段の表は §12、受入と試験は §13、未決は §14。
- **C (ACTIVE でない側の TVRAM のコンソール) は HAL v2 の段に入れない** — 別の票 TASK_CONSOLE_DUAL (確定設計 [CONSOLE_DUAL_DESIGN](../../decisions/gui/CONSOLE_DUAL_DESIGN.md)) が受け持つ (PM 決定、第 4 回のレビューの P1)。受け持つ経路は確定設計 §9-1、暫定の口 gf_outputs の差し替えは段 K1。
- 描画の高速化 (票 TASK_GFX_ACCEL_HAL) の上に乗る。G3 (GeForce の DIRECT) までが入ってから A0 に着手する。

## 2. 段の状態
| 段 | 状態 |
|---|---|
| A0 (宣言だけ) | main (e00f3ce2、NP21/W 40/40) |
| I1 の材料 C1・C3〜C7・C15〜C18・M1〜M3・X1・G5・G9 | 実装・host 試験済み (wt/halv2-i1) |
| I2 の材料 C2・C8〜C10・C14・S1〜S11・G1・G4・G7・G8・U1 (G3・G6 は廃止、色鍵と α は sw) | 実装中 |
| I3 の材料 C11〜C13・G2・P1・T1・Y1〜Y4・D1 | 未着手 (G2・D1 は設計の往復が先) |
| K1 (CONSOLE_DUAL の gf_outputs の差し替え) | CONSOLE_DUAL の D2 に吸収 (2026-11-12、gf_outputs は作らない、CONSOLE_DUAL_DESIGN の末尾の追記) |
| I1 (面の道) | 結線済み・NP21/W の一括受入は合。ゲスト試験 gfx2_i1 が FAIL で調べ中 |
| I2 (描画の道)・I3 (表示の道) | 未着手 |
| CONSOLE_DUAL (決定 C) | 確定設計あり ([CONSOLE_DUAL_DESIGN](../../decisions/gui/CONSOLE_DUAL_DESIGN.md)、段は TASK_CONSOLE_DUAL) |

注意: TASK_PPC_SEP の予定の段 3 番 (gfx/gfx_core.c の `g_backend == &gfx_backend_geforce/trident` の分岐を provider へ移す) と 1 番 (gfx/gfx_vram.c)、TASK_GFX_ACCEL_HAL の S1 (gfx/gfx_vram.c) は、C1・C14 と同じ所に触る。これらと C1・C14 を同時に走らせない。

## 3. 受入の仕分け (ユーザー決定 2026-11-11「はい、その形で進めて」)
NP21/W に GeForce・Trident は無く PEGC だけなので、段ごとに確かめる場所を分ける。

| 区分 | 段 | 確かめる場所 |
|---|---|---|
| E (NP21/W で足りる) | 中核の C・SW の S・M・X・U1・P1 (PEGC)・KAPI の道 | NP21/W の一括受入 |
| R (実機だけ) | G1・G2・G4・G5・G7・G8・G9 (GeForce 6200 = NV44A)、T1 (Trident)、Y1〜Y4 の待ち時間、bus master の PCI のブリッジ越えの到達 | 実機 Ra266 |
| E+R | 結線 I1〜I3 のゲスト試験 | NP21/W は PEGC で、実機は `GFX=geforce` と `GFX=trident` で同じ試験を回す |

- 実機の受入は段ごとにせず、I1・I2・I3 が main に入るたびに 1 回まとめて回す。ノートのセッションに頼み、判定はシリアル (ゲスト試験の PASS/FAIL・mem・起動ログ) と、画面はカメラで PM が見る。
- R の項目は延期の台帳 DEFERRED_TESTS の HALV2-RH-* の行で持ち、節目ごとに閉じる。
- 2026-11-12: I1〜I3 の R の項目は実機 (main ee876302) で GeForce・Trident とも gfx2_i1〜i3・gfx2_rh が PASS し、HALV2-RH-I2・RH-I3 を閉じた (証拠はノートの ~/os32-tmp/evidence/2026-11-12/halv2-i2-idle/ ほか)。K1 は CONSOLE_DUAL の D2 に吸収した。

