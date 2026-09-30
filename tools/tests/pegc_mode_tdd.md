# pegc_mode — PEGC 640x480 へ入る / 戻る OUT 列と GDC の FIFO 待ち (ホスト TDD の記録)

票: [docs/tasks/realhw/TASK_PEGC480_REALHW.md](../../docs/tasks/realhw/TASK_PEGC480_REALHW.md) §2 (H2・H3・H5)、§4 /
[docs/archive/realhw_v21/TASK_PEGC_RA266_TIMING.md](../../docs/archive/realhw_v21/TASK_PEGC_RA266_TIMING.md) §4
実装: `gfx/backend_pegc.c` の `pegc_apply_timing()` / `gdc_send()` / `pegc_boot_sync_record()` / `pegc_text_sync_400()`、
値は `include/pegc.h` §10 (実機の記録で差し替える 1 か所)・§11・§12
試験: `tools/tests/pegc_mode_host.c` + `tools/tests/test_pegc_mode.py` + `tools/tests/pegc_hostshim/io.h`
(`make check-pegc-mode-host`)

## 欠陥 (直す前)

実機 PC-9821Ra266 で `gfxmode pegc` の GUI が崩れる。候補のうち実装で潰せるもの:

- **H5**: `pegc_init` はグラフィック GDC の PITCH と GDC クロック (6Ah 82h〜85h) を設定せず、起動時の BIOS 状態
  (NP21/W では PITCH 40 + 2.5MHz = 実効 640 バイト/ライン) に頼っていた。NP21/W の BIOS の AH=30h は 480 ラインへ
  入るとき **PITCH 80・5MHz** にする (票 §3「段 1 の NP21/W での記録」)。戻り側も同じく触っていなかった。
- **H3**: `gdc_send` は GDC のステータス (FIFO FULL / EMPTY) を見ずに 9 バイト流していた。
- **H2**: 順序の置き場が `pegc_init` と `pegc_gdc_set_timing` に分かれ、実機の ROM の列と比べて並べ替える
  1 か所が無かった。

## 様式

実物の `gfx/backend_pegc.c` を 1 行も写さずに `#include` し、`io.h` だけを `-I` の先に置いた偽物へ差し替える
(`atapi_hostshim` と同じ作法)。偽の I/O は IN / OUT を順に記録し、GDC のステータスはケースごとに
「FULL を返す回数」「EMPTY を返さない回数」「コマンドを書いた直後から FULL にする回数」を決めて返す。
09A0h は sel 09h のとき bit0 = CLOCK-1、常に bit1 = CLOCK-2 を返す ([U] io_disp.md I/O 09A0h)。
期待列は試験側で `pegc_apply_timing` の順序を**独立に書き下し**、値は `include/pegc.h` のマクロから組む
(実機の値へ差し替えても期待列は直さなくてよい。`defaults` だけが NP21/W の数値を固定する)。

## ケース (10 本、1 実行で全部)

| ケース | 見るもの |
|---|---|
| `restore_matrix` | **資料の規則で出た OUT を独立に検査** (Codex レビュー P2、下の節)。起動時 24kHz / 31kHz × クロック 2.5MHz / 5MHz / 片方だけ 5MHz の 6 通りを `pegc_restore_text_sync` と `pegc_text_sync_400` の両方の口で、加えて 480 ラインへ入る側を 5MHz の組で |
| `defaults` | 既定値 = NP21/W の記録と資料 (PITCH 480 = 80、6Ah 83h + 85h、戻りの PITCH 40 / 80、09A0h の sel / bit、待ちの上限 < 100ms、SCROLL P4 = 40h、戻りの SYNC 4 組 = NP21/W gdcslavesync の 24-L / 24-M / 31-L / 31-M、戻りの SCROLL の IM)。**実機の記録で §10 を差し替えたらここを直す** |
| `enter_480` | 入る列: 09A8h 01h → 6Ah 83h, 85h → 62h 0Eh + 8 → A2h 0Eh + 8 → A2h 47h + 50h → A2h 70h + 4 → 68h 0Fh → 62h 0Dh → A2h 0Dh → 6Ah 07/69/06 → 6Ah 07/21/06。起動時が 31kHz・5MHz でも同じ列 (起動時の状態に依らない) |
| `restore_31k` | `pegc_restore_text_sync(1)` (起動時 81h・2.5MHz = 実機 Ra266 の CUI の形): 20h / 68h → 09A8h 01h → 6Ah 82h, 84h → 31kHz 400 の SYNC (グラフィックは 2.5MHz 用 31-L) → PITCH 40 → SCROLL (IM=0) → 表示開始 → A2h 0Ch |
| `restore_24k_5m` | 起動時 24kHz・両方 5MHz → 83h, 85h・5MHz 用 SYNC (24-M)・PITCH 80・SCROLL IM=1 で戻す |
| `boot_clock` | 片方だけ 5MHz は 2.5MHz 扱い (PITCH 40)。09A0h へ 09h を書いてから読む。2 回目の記録は何もしない |
| `restore_unrecorded` | probe が通っていない (起動時のクロックを読んでいない) 戻りはクロック・PITCH に触らない = 従来の列 (5MHz 用 SYNC + IM=0) |
| `hsync_bits` | 起動時の読みが FFh でも 09A8h へは 01h しか書かない ([U] 09A8h「bit 7〜2 は常に 0」) |
| `fifo_busy` | 詰まっている間は書かない。最初のコマンドの前に EMPTY を 5 回待つ (待ち 5 回)。SYNC のコマンド直後に FULL ×4 → 最初のパラメータの前に 5 回読む。列は変わらない |
| `fifo_stuck` | 詰まったまま: 1 バイトごとにちょうど `PEGC_GDC_FIFO_POLLS` 回読んで諦め、打ち切りをバイト数だけ数え、列は変わらない (無限ループにならない) |

全ケースで、GDC (60h/62h/A0h/A2h) への書き込みの**直前の事象がその GDC のステータスの読み**で、コマンドなら
EMPTY、パラメータなら FULL でないこと (`check_fifo_gate`) を見る。

## RED → GREEN

- RED: 試験を先に書いた時点では `pegc_enter_480_ports` / `pegc_apply_timing` / `s_boot_clk*` が無くコンパイルが通らない。
  旧実装の振る舞い (PITCH・クロックを出さない / FIFO を待たない / START・STOP を直に書く) は下の変異
  `clock_not_set_on_enter`・`pitch_skipped`・`no_clk2`・`no_wait_cmd`・`no_wait_prm`・`start_bypasses_fifo`・`stop_bypasses_fifo`
  として残し、どれも RED になることを確かめた。
- GREEN: `PASS pegc_mode_host (9 cases)`、`TARGET i386-elf GNU89 -Werror COMPILE PASS`。

## 変異 (34 本、全部 RED、2026-09-29)

| 群 | 変異 |
|---|---|
| FIFO 待ち (H3) | コマンド前の待ちを消す / パラメータ前の待ちを消す / パラメータで FULL を待つ (逆) / コマンドで EMPTY でなく非 FULL を待つ / テキスト GDC のステータスを A0h から読む / 上限を 1 回多く / 打ち切りを数えない / 読み直しの間に待たない / STOP・START を FIFO を見ずに直に書く |
| 順序 (H2) | クロックを SYNC の後へ / 09A8h を出さない / 6Ah 69h・21h をタイミングより前へ |
| 値 (H5) | CLOCK-2 (85h) を出さない (NP21/W の BIOS の列と同じ誤り) / 480 の PITCH を 40 に / PITCH を送らない / 入るときクロックに触らない / 2.5MHz の値の取り違え / 片方 5MHz で PITCH 80 / 戻りで起動時のクロックを見ない / 記録の無い戻りでクロックに触る / CLOCK-2 を bit0 から読む / 09A0h に 09h を書かずに読む / 起動時の 09A8h の bit7 を書き戻す / 31kHz の戻りで 24kHz の SYNC |

| 戻りの組 (Codex P2) | 戻りのグラフィック SYNC を常に 5MHz 用 (24kHz / 31kHz それぞれ) / 常に 2.5MHz 用 / SCROLL を常に IM=0 / 常に IM=1 / ヘッダの 2.5MHz 用 SYNC の C/R を 4Eh に / HBP を 07h に / 480 の SCROLL の IM を落とす / 400 の 5MHz 用 SCROLL の IM を落とす |

`python3 -B tools/tests/test_pegc_mode.py --mutate` の CONTROL (変異なしの写しの木) は GREEN。

## ホストで見られないもの

- 実機の ROM が流す値・順序そのもの (`v86 -g` の記録待ち — 票 §3 段 1)。この試験は「決めた値と順序どおりに出す」
  ことと「差し替えが 1 か所で済む」ことまでしか見ない。
- 実機の GDC の FIFO が本当に詰まるか、10ms/バイトの上限で足りるか (`pegc_gdc_fifo_timeouts` を実機の kernel.map で読む)。
- NP21/W 上の表示 (回帰は PM / テスターのエミュレータ確認)。

## Codex レビュー P2 (2026-09-29) — 戻りの SYNC・SCROLL をクロックとの組で

指摘: 戻りでクロックと PITCH は起動時の値から選ぶのに、グラフィック GDC の SYNC は常に 5MHz 用 (C/R 4Eh)、
SCROLL は常に IM=0 だった。起動時 2.5MHz の機械 (NP21/W もこれ) では「PITCH 40 + 5MHz の SYNC」、起動時 5MHz なら
IM=1 が失われる。最後にグラフィック GDC を STOP するので、CUI が見えることでは成功を確かめられない。
**期待列をヘッダの定数から組んでいたので、ヘッダの選び方の誤りは試験に見えなかった。**

- 直し: `include/pegc.h` §10 に 24kHz / 31kHz × 2.5MHz / 5MHz の 4 組 (グラフィック SYNC) と SCROLL 2 組 (IM=0 / 1)。
  24kHz の 2 組は [B] 2-6 表2-27 と一致 (NP21/W 24-L / 24-M)、31kHz は NP21/W 31-L / 31-M。`pegc_text_sync_400` は
  起動時のクロック (両方 5MHz かどうか) で組を選ぶ。記録が無いときは従来の組。
- 試験: `restore_matrix` が出た OUT からグラフィック GDC の SYNC・SCROLL・PITCH と 6Ah を拾い、**試験側に直に書いた資料の
  数値** (C/R 26h / 4Eh、IM、PITCH 40 / 80、24kHz は表2-27 の HS・HFP・HBP・VS・VFP・VBP・L/F 全部) と照らす。ヘッダを
  見ないので、ヘッダの値の誤りも選び方の誤りも落ちる (変異 9 本を足し、どれも `restore_matrix` で RED)。
- RED: 直す前の選び方 (常に 5MHz 用 SYNC / 常に IM=0) は変異 `restore_ssync_always_5m`・`restore_scroll_always_im0` として
  残し、`restore_matrix` の C/R・IM の行で落ちることを確かめた。

## 2026-09-29 — 実機 ROM の列に合わせる (受け入れ A、wt/pegc480-real)

票 §3-3 の比較で、SYNC の値は実機の ROM と一致、違いは順序と出していなかったコマンド (RESET・MASTER/SLAVE・CSRFORM・ZOOM・
テキスト GDC の PITCH/SCROLL・表示停止・6Ah 41h・6Eh・CRTC・START/STOP2) と SCROLL の LEN だった。`pegc_apply_timing` を ROM の列の順に
書き直し、試験を作り直した。**上の節 (10 ケース・34 本) は旧版の記録**。

### 様式の変更

- 実機の `v86 -g` の記録 (hw の O 行、s480・back 各 94 行) を試験の `ROM_S480` / `ROM_BACK` に**数値で直に**書き、実物の
  backend_pegc.c の OUT 列と 1 行ずつ突き合わせる。違えてよい行は `EDIT_COMMON` (DROP 9 行: 09A0h 03h、6Ah 07h/A0h DFh/A2h 28h/6Ah 06h、
  A2h 20h/78h + 2)、`EDIT_BACK` (SET: 68h 09h → 08h)、`ADD_S480` / `ADD_BACK` (最後の START) に理由付きで置く。5Fh (io_wait) は比べない
  (記録器が捕まえない) が、6Eh の直後に 5Fh があることは別に見る。
- 独立の書き下し `x_mode` (順序を試験側でもう一度書く) が Ra266 の値で ROM の列と一致することを確かめ、その上で記録の無い組
  (起動時 24kHz・起動時 5MHz・probe なし) の期待列に使う。
- 偽の I/O に待ち (cpu_delay_us) とカーソル (console_hw_cursor_enable) の事象を足し、FIFO の待ち (2us) と VSYNC の待ち (10us) を数え分ける。
  テキスト GDC のステータスの VSYNC は 2 回ごとに反転し、OUT と OUT の間で VSYNC が落ちた回数で**待ちの位置** (7 か所、ROM が 60h を
  読み続けた所) を見る。

### ケース (12 本)

| ケース | 見るもの |
|---|---|
| `restore_matrix` | 戻り 6 通り × 2 口 + 入る側: グラフィック SYNC の C/R・SCROLL の IM と **LEN = 3FFh**・PITCH・クロックを資料と記録の数値で |
| `defaults` | ヘッダ §10・§13 の値 = 記録 / 資料の数値 (試験側に直書き) |
| `rom_s480` | **受け入れ A**: 起動時 81h・2.5MHz で 480 へ入る列 = ROM s480 (EDIT 適用)。09A8h・emu 行のポートに書かない。VSYNC 待ちの位置 7 か所 |
| `rom_back` | **受け入れ A**: 戻る列 = ROM back (EDIT 適用)、`pegc_shutdown` の口と v86 -g FALLBACK の口の両方。CUI ならカーソルを戻し、GUI (シンク有効) なら戻さない |
| `enter_24k` | 起動時 24kHz: ROM s480 + 「68h 0Eh の後に 09A8h 01h」。戻りは 24kHz の組・6Eh 20h・09A8h 00h |
| `restore_24k_5m` / `boot_clock` | 起動時のクロックで戻りの組 (83h/85h・24-M・PITCH 80・IM 1、片方だけ 5MHz は 2.5MHz 扱い) |
| `restore_unrecorded` | probe が通らない機種: 6Ah・6Eh・09A0h に書かない、クロック・PITCH に触らない |
| `hsync_bits` | 09A8h は bit1,0 だけ、今の読みと同じなら書かない |
| `fifo_busy` / `fifo_stuck` | FIFO 待ち (旧と同じ規則。上限で打ち切って数え、列は変わらない) |
| `vsync_stuck` | VSYNC が来ない / 明けない: 待ちごとに 1 辺だけ上限まで読んで数え (7)、列は変わらない |

RED → GREEN: 新しい試験は新しい記号 (`pegc_vsync_timeouts`・`PEGC_XATTR_*` など) を使うので、旧実装 (d5cd3a5) ではコンパイルが
通らない (旧の列そのものを新しい試験で回したことは無い)。旧の振る舞いの断片 (09A8h を毎回書く・RESET を出さない・CSRFORM を出さない・
LEN 0 など) は変異 `hsync_always_written`・`no_gfx_reset`・`no_text_csrform`・`no_gfx_csrform`・`hdr_scroll_480_len0` 等として残し、
どれも RED。GREEN: `PASS pegc_mode_host (12 cases)`。

### 変異 (74 本、全部 RED、2026-09-29 — P2 対応で 76 本、下)

FIFO 9 / VSYNC 7 / 順序と ROM の列 26 / 値 11 / 戻りの組 6 / ヘッダの値 15。どの変異がどのケースで落ちるかは
`python3 -B tools/tests/test_pegc_mode.py --mutate` の出力 (受け入れ A の `rom_s480` / `rom_back` で落ちるのが 35 本)。CONTROL は GREEN。

### ホストで見られないもの

- 実機で表示が正しくなるか (受け入れ A は「ROM と同じ列を出す」まで)。実機では `pegcchk` (票 §5 (C)) の行で、09A8h の bit7 が残るか・
  クロックが戻るか・FIFO / VSYNC の打ち切りが 0 かを見る。
- ROM の VSYNC 待ちの正確な条件 (ROM を逆アセンブルしていない)。

## 2026-09-29 — Codex レビュー P2 ×2 への対応

- **P2-1 (A0h DFh / A2h 28h の除外理由)**: 「[HW1] の描画」「次の RESET1 が FIFO を捨てる」は記録から裏付けられない、の指摘。資料を当たり直し
  (票 §3-3「意図的に違える点」)、「モード設定の一部」とは読めない (A2h 28h は [B] 表2-26 の描画制御 WRITE、6Ah 07h/06h が A0h/A2h の
  意味を変えるとの記述なし、DFh はコマンドより先のパラメータ) ので**送らないまま**、試験の `EDIT_COMMON` の 4 行を `UNRESOLVED:` と明記した
  (未解明の差分)。`defaults` が「UNRESOLVED は ROM の 8〜11 行目の 4 行だけ」を確かめる。RESET1 で打ち消されるという主張は取り下げた。
- **P2-2 (カーソル復帰の OUT が比較から漏れていた)**: 試験は `console_hw_cursor_enable()` を OUT の無い印に置き換えていた。
  **実物の kernel/console.c を #include** し (カーネルは -Wall だけで組むので、試験の -Wextra -Werror で出る console.c 側の 2 警告だけ
  pragma で止める)、CSRFORM + CSRW の実際の OUT を期待列に `ADD_CURSOR` (意図的な追加) として足した。論理カーソルは (5, 3) に置いて
  CSRW の番地 00F5h まで見る。GUI から抜ける途中 (シンク有効) と V86 中 (FALLBACK の口) では出ないことも見る。FIFO の門は
  backend_pegc.c が出す分 (最後の 62h 6Bh まで) だけを見る (console.c は従来から FIFO を見ない — 範囲外)。
- (A) が比較する区間を試験の頭の注記と票 §3-3「(A) が比較する区間」に書いた。
- 変異 +2 (`cursor_before_start`・`console_cursor_no_dc`、どちらも `rom_back` で RED) で 76 本。
