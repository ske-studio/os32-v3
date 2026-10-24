# TASK_FONTPERF — 標準フォントの描画を D15 に届かせる (描き済みの字形の束・枠の詰め直し・先読み・領域の拡大・ラスタの整数化)

> 状態: **受入完了・実機確認待ち (2026-10-21、F1〜F6 着地・NP21/W と Ra266 で計測、残りは画面確認の項目 = 台帳 T7B-REALHW)** — T7 の後、KPEEK より先に行う (ユーザー 2026-10-20「先にフォントを直しておきましょう」)。

## 0. ユーザー決定と背景 (2026-10-20)

- 実機 Ra266 (b4216e6) の計測: 未 cache の漢字が散った 40 字の text() が最大 300ms (D15 は 100ms)。1 字あたり seq 約 1.0ms・scatter 約 6.6ms で、差の約 5.6ms は読み出しの外れ (a7 で scatter は 1 字 約 2.9 回の read_at)。2 周目は 0.02ms 以下。**遅さの主因は I/O で、ラスタは 1 字 1ms 未満**。証拠はノートの `~/os32-tmp/evidence/2026-10-20/realhw_t7b/`、台帳 T7B-REALHW。
- 採る順 (ユーザー承認、PM の推奨どおり):
  1. **ⓐ 描き済みの字形の束** — 標準フォント (東雲 16) の 16px 1bpp 字形をビルド時にホストで作り、コード順に並べたファイル (例 `/usr/share/fonts/shinonome16.g16`、7,059 字 × 32B ≈ 226KB + 索引) として配る。束にある字は TTF の解析もラスタも通らず、1 字の読み出しは高々 1 回。**元の TTF と版 (sha) で照合し、合わなければ使わない「ディスク上の cache」** — システムフォントを TrueType で差し替えられるという決定 (T7 票) は変えない。差し替えたフォントは TTF の経路。
  2. **① 字形 cache の枠の詰め直し** — 16×16 にちょうど収まる字のための詰めた枠 (目安 ヘッダ 8B + 字形 32B) を足し、64KiB の arena で 682 字 → 約 1,600 字。はみ出す字は今の大きな枠。
  3. **ⓑ 起動時の先読み** — かな・記号・英数・常用漢字の上位・UI の字・FEP 辞書の頻度の上位から 1,000〜1,500 字を、gshell の起動時に束から arena へ入れる。
  4. **② 領域の拡大** — gshell は像の空き (約 160KB) から arena を 128KiB に、app は 128〜256KiB に。値は limits.rs の 1 か所で、実測で決める。
  5. **ラスタの整数化** — T7 票 §4 の後の項目 (os32_math、x87 guard の撤去、a5 の受入のやり直し)。D15 への効き目は小さいが FPU 無しの機体と作りの簡素化のため。
- ⓒ 使った字の記録と次の起動での先読みは後回し。

## 1. 詳細設計 (確定 2026-10-20、Fable 起草・Opus 2 往復・PM が最終修正、ユーザー判断は無し)

全文 (束の形式・clock・先読み・整数化の式・試験・変更一覧): `~/os32-tmp/run/fontperf/DESIGN.final.md`。

| 段 | 中身 | 並列 |
|---|---|---|
| F1 | ⓐ ホスト: 束 `.g16` の生成 (32B の記録を 512B に整列、索引と CRC32、元 TTF の head 12B)、先読み一覧 (IPADIC のコスト順 + JIS 1〜8 区 + UI の字)、配備・CI・THIRD_PARTY | F2 と並列 |
| F2 | ①+② 字形 cache: 16×16 の詰めた枠 (Cell)・class ごとの clock (空きは詰めて埋める)・gshell の arena を static へ・app 128KiB | F1 と並列 |
| F3 | ⓐ ゲスト + ⓑ 先読み: 束の読み (px16 だけ、cp の照合、閉じる経路)、起動時の先読み、shell の lease +1、Stats の追加は末尾だけ | F1・F2 の後 |
| F4 | 計測 (NP21/W → Ra266): D15 (40 字の text() の最大)、起動時間 (0.5s 以内)、満杯の commit、app arena と先読み字数の決定。超えたときの手は全文 §8 | F3 の後 |
| F5 | ⑤ ラスタの整数化 (os32_math、ttf-parser は dev-dependency だけ) と a5 の受入のやり直し | F1〜F3 と並列 |
| F6 | x87 guard の撤去 (font_guard・NOSYS・fp_scope、`Reason::NoFpu` は値を残す)、objdump で x87 命令 0 件 | F5 の後 |
| F7 | 受入の記録、NP21/W で FPU 無し 1 回、Ra266 の再計測、節目の全体検査 | 最後 |

PM 決定: 束の照合は head 12B と索引の CRC32、先読み一覧は IPADIC (THIRD_PARTY の行) のコスト順 + 公的字表 + UI、起動 +0.2〜0.25s を許容 (Ra266 で 0.5s 以内、超えたら 1,000 字)、shell の lease +1、app arena 128KiB・先読み 1,600 字 (F4 で決め直す)、memmap の再割りは像の残りが 10KB 未満のときだけ。字形 cache の追い出しは age 近似 LRU から clock に替えた。

## 2. 受入 (2026-10-21)

- 段: F1 (束の生成)・F2 (Cell 枠と clock)・F3 (束の読みと先読み、lease 3)・F3b (gshell の像の上限 0xA5000、sbrk の余裕 64KiB、世代 4 のまま)・F5 (整数化)・F6 (x87 guard の撤去) が main に着地 (8736b65 まで)。各段は Opus 実装・Fable レビュー。
- **Ra266 (8736b65、ノートの `~/os32-tmp/evidence/2026-10-20/realhw_fontperf/`)**: 1 回の text() (40 字) の最大 — scatter **300ms → 100ms**、seq 80 → 50ms (tick 10ms)。cold 1,000 字 — scatter 6.61 → 1.55 秒、seq 1.04 → 0.88 秒。warm は 0.02 秒。束 ok・reject 0・err 0、先読み 1,600、raster 0 (全字が束から)。FONTCHK 2 行一致、font_lease_test lease 11/11・limit 5/5 (上限 3 本)、SERVICES_READY・部品 9 個 rc=0・leftover 0。
- **NP21/W**: `~/os32-tmp/evidence/2026-10-20/fpf3/`・`fpf3b/`・`fpf6/` (scatter cold 6.80 → 1.03 秒、text() の最大 290 → 50ms、gshell の exec_heap peak 131,656 B、x87 命令は gshell・libos32gui とも objdump で 0 件)。
- **D15 (100ms)**: Ra266 で散った未 cache の漢字だけの 40 字が上限ちょうど (10 tick)。先読みの 1,600 字に当たる普通の文はさらに速い。P100 では超える見込みが残る → 台帳 T7B-REALHW に「P100 または Ra266 の画面で入力→描画を測る」として残す。
- **確かめられないもの**: FPU 無しの構成 — NP21/W に FPU を外す設定が無く (CPU feature の FPU bit は情報表示だけ)、実機 2 台とも FPU あり。x87 命令 0 件 (objdump、試験 check-font-host・check-font-guard-host の変異) を根拠にする。gshell の起動時間 (NP21/W 0.63 秒、Ra266 は画面が要る) は画面確認の項目。
