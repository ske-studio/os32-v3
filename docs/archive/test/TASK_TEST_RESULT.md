# TASK_TEST_RESULT — 合否を機械が読める形にする (ゲスト試験ランナーの 2 段目)

> 状態: **受入完了 (2026-09-17)** — 追補は §11 (集計行は fd 1 へ)。(2026-09-29 の棚卸しで更新)
>
> 発行: PM (Claude Code `claude-opus-5`、2026-09-17) / それまでの状態: **追補あり (2026-09-17)** — ゲスト受入は §9、見つけた不具合は §10、**集計行の出し先は §11**

基点: `feat/gui` = `8c87075`。
引き継ぎ: [`../agents/HANDOVER_2026-09-16.md`](../agents/HANDOVER_2026-09-16.md) §7-2 の 2 段目。
前段: [`../shell/TASK_EXIT_STATUS.md`](../shell/TASK_EXIT_STATUS.md) (**受入完了**。`$?` と `exec_last_result` は landed、KAPI v55)。
後段: ランナー (`TASK_TEST_RUNNER.md`、未起票)。
決裁済み: **R1** = 結果チャネルは **HostDrv** (PM、2026-09-17。§3 に根拠)。

## 0. 目的と範囲

ゲストの試験プログラムの合否を、**人が画面を読まずに判定できる形**にする。
ランナー (3 段目) はこの約束事だけを見る。

**この票の範囲は「約束事の制定」と「第 1 陣の適合」まで。** 44 本すべての適合は範囲外 (§8)。

## 1. 確認済みの事実 (調査 2026-09-17、実数)

`userland/tests/*.c` は **44 本**。`main()` を持つのは **41 本** (残り 3 本は
`_start()` + `int 0x80` の自己完結バイナリ)。

| 事実 | 本数 | 影響 |
|---|---|---|
| **`void main`** | **20** | `sdk/crt/crt0_c.c` は `int main` として呼び `sys_exit(ret)` するので、**終了コードが eax の残骸**になる。再現性が無い |
| `int main` で失敗時に非ゼロを返す | 13 | そのまま使える |
| `int main` だが常に `return 0` | 8 | **集計変数は持っているのに載せていない**。1 行ずつで直る |

終了コードはカーネル側で切られない (`crt0_c.c` → `wrap_sys_exit` → `kapi_sys_exit` →
`exec_exit_status` はすべて 32 ビットの `int`)。**下位 8 ビットに丸めるのはシェルの
`sh_status_from_kind()` (`userland/shell/main.c:30`) だけ**。

`$?` の予約値 (前段で landed、`userland/shell/shell.h:76`):

| `$?` | 意味 |
|---|---|
| 126 | 起こせなかった (ヘッダ不正 / メモリ不足 / その他) |
| 127 | 実行ファイルが見つからない |
| 130 | CTRL+STOP / ESC で中断 |
| 139 | 例外 (#PF / #GP) で畳んだ |

→ **試験プログラムがこれらを返すと、落ちたのか自分で落ちたのか区別できなくなる。**

## 2. 約束事 (制定)

### 2-1. 終了コード

| 値 | 意味 |
|---|---|
| **0** | 全項目合格 |
| **1** | 1 件以上不合格 |
| **2** | **実行しなかった** (前提の欠如、引数不正、対象が無い) — 不合格とは区別する |
| 3〜125 | 個別の票が定義してよい (定義しないなら使わない) |
| 126 / 127 / 130 / 139 | **予約。試験は返さない** (§1 の表) |

`main` は必ず **`int main(int argc, char **argv, KernelAPI *api)`** で宣言する。
`void main` は約束違反とする。

### 2-2. 集計行 (最終行に 1 行)

    <名前>: PASS <合格数>/<総数>
    <名前>: FAIL <合格数>/<総数>
    <名前>: SKIP <理由>          前提が無くて実行しなかった (終了コード 2 と対)

`<名前>` はプログラム名 (`argv[0]` の basename ではなく**固定文字列**。リダイレクト先や
引数で変わらない)。既存の `host_test` / `db_v50_test` がこの形なので、それに揃える
(`=== Result: %d/%d passed ===` 形式より grep しやすく、名前が行に載る)。

**終了コードと集計行は必ず一致させる。** 片方だけ直すと、ランナーが 2 つの答えを持つ。

**出し先は fd 1** — この節は当初それを書き漏らしており、16 本中 14 本が
`api->kprintf` (リダイレクトを素通りする画面出力) で出していた。経緯と決め直しは
**§11**。行を出すのは `userland/lib/rt/testresult.h` の
`os32_test_summary()` / `os32_test_summary_skip()` で、呼び手は行を受け取らない。

### 2-3. 詳細ファイル (任意、ただし書くならこの形)

    /host/test/<名前>.txt

1 行 1 件、`PASS <項目名>` または `FAIL <項目名> <理由>`。
最終行は 2-2 の集計行と同一。**書けなくても試験の合否は変えない** (§3)。

## 3. 結果チャネル — HostDrv (決裁 R1)

ゲストが `/host` へ書いたファイルはホストの `C:\os32` で即座に読める
(2026-09-17 に kstring 計測の実データ 3 本をこの経路で回収済み)。
画面の読み取り (tvram) は文言に依存するため [V4] と衝突するので採らない。

**`/host` は常にあるとは限らない。** `kernel/kernel.c:318` は
`hostdrvfs_detect()` が真のときだけ `vfs_mount("/host", ...)` する。実機には HostDrv が無い。

| 状況 | 試験プログラム | ランナー (3 段目) |
|---|---|---|
| `/host` あり | `/host/test/<名前>.txt` に書く | ホスト側で集計する |
| `/host` なし | **詳細ファイルを諦める。集計行と終了コードは必ず出す** | **`/host` の不在を明示して非ゼロで終わる。合格にしない** |

書き込み先のディレクトリは**冪等に作る** (`sys_mkdir` が「既にある」を返すのは成功扱い)。
`/tmp` は既存の ext2 イメージにはあるが、**インストーラ (`userland/system/install.c`) が
作るのは `/sys` `/bin` `/sbin` `/etc` の 4 つだけ**で、新規インストール直後にあるとは限らない。
`/tmp` を前提にしない。

## 4. 第 1 陣 — 適合させるもの

回帰台本 (`tools/emu_agent/tasks/regress.txt`) に載っているものと、
**集計を持っているのに終了コードに載せていない 8 本**を先に通す。

| 対象 | 今の状態 | やること |
|---|---|---|
| `klibc_test` | 終了コードは正しい。集計行は `=== Result: %d passed, %d failed ===` | 集計行を 2-2 の形に |
| `db_test` `math_test` `mgx_test` `save_test` `restest` `stat_t` | `int main` だが常に 0 | 集計を終了コードに載せ、集計行を 2-2 の形に |
| `asset_test` `gui_call_test` `test2` | **`void main`** なのに集計あり | `int main` にしたうえで上と同じ |
| `e2test` `ecs_test` `db_v50_test` `host_test` `input_test` `font_load_test` | 終了コードは正しい | 集計行を 2-2 の形に。`font_load_test` は**負値を返している**ので 0/1/2 に直す |

`/host` を決め打ちしている `save_test` (3 パス 9 か所) は、`/host` が無ければ
**`SKIP` + 終了コード 2** にする (今は全滅する)。

## 5. 直す欠陥 (調査で出た。両方とも確認済み、推測ではない)

### 5-1. `alloc_demo` が何も検査していない

`userland/rust/alloc_demo/src/lib.rs` は `extern "C" fn main(...) -> i32` で
**常に `0` を返し、最後に無条件で `All tests passed!` を印字する**。
`sum=285` も「expected 285」とコメントに書くだけで**比較していない**。

回帰台本がこれを「最後の 3 行を読め」としているのは、機械判定できないため。
**つまり回帰台本に偽の合格が 1 件混ざっている** ([V4])。
実際に検査して 2-1 / 2-2 に適合させる。

### 5-2. `ring3_fault` / `ring3_hello` が `sys_exit` ではなく `gfx_init` を呼ぶ

`int 0x80` の `eax` は**KAPI スロット番号**で、スロット 0 は `gfx_init`、
`sys_exit` は **84** (`KAPI_SLOT_SYS_EXIT`)。
`ring3_fault.c:51` と `ring3_hello.c:54` は `eax=0` のまま = M1 の古い規約。

    __asm__ __volatile__("int $0x80" : : "a"(0), "b"(0) : "memory");

**同じ誤りは `ring3_guard.c` で 2026-09-06 に直されており、その経緯が
`ring3_guard.c:105-110` のコメントに残っている** (「かつては eax=0 で呼んでいたが、
それはスロット 0 = gfx_init であって終了しない。ケース E で GFX モードに入ったまま
無限ループ、CTRL+STOP で回収した」)。**2 本が修正から漏れた。**

`ring3_guard.c` と同じ形に直す (`KAPI_SLOT_SYS_EXIT` + 引数をユーザスタックへ積む)。
`ring3_hello` は正常終了が正解なので**実害がある** (ゲストが固まる)。
`ring3_fault` の到達点は「既に失敗」なので実害は小さいが、同じ理由で直す。

**この 2 本は終了コード方式に載らない** (分類 (d): 落ちるのが正解)。
マーカー番地と `fault_kill_count` で判定する既存方式のまま、3 段目で別枠にする。

## 6. 段取り

1. §2 の約束事を `docs/` に置く前に、**ホスト試験で約束事そのものを固定する**
   (`tools/tests/test_result_conv_host.c` + `test_result_conv.py`、`make check-result-conv-host`)。
   見るもの: 終了コードの値域 (126/127/130/139 を返さない)、集計行の書式、
   **終了コードと集計行が一致すること**、`void main` が 1 本も残っていないこと
   (`userland/tests/*.c` を走査する静的検査)。
2. §5 の欠陥 2 件を直す。`ring3_*` は `ring3_guard.c` の形をそのまま写す。
3. §4 の第 1 陣を適合させる。
4. `make check` に足す。

## 7. 受入

| ID | 反例・操作 | 期待 |
|---|---|---|
| T1 | `make check-result-conv-host` | GREEN。変異 (集計行と終了コードを食い違わせる / 予約値を返す / `void main` を戻す) で RED |
| T2 | 第 1 陣を 1 本ずつゲストで実行し `$?` を見る | 全合格なら 0、わざと壊せば 1、前提を外せば 2 |
| T3 | 集計行を grep して `<名前>: (PASS\|FAIL\|SKIP)` に一致 | 第 1 陣すべてで一致 |
| T4 | `alloc_demo` の検査対象をわざと壊す | `FAIL` になり終了コード 1。**壊す前は 0** |
| T5 | `ring3_hello` をゲストで実行 | **戻ってくる** (今は固まる)。`$?` が 0 |
| T6 | `/host` を持たない構成 (または `/host` を伏せて) 第 1 陣を実行 | 集計行と終了コードは出る。詳細ファイルの失敗で合否が変わらない |

T5 と T6 は実機 / エミュレータでの確認が要る (PM が行う。コーダーはホストまで)。

## 8. この票でしないこと

- **44 本すべての適合**。第 1 陣 (§4) の外は次の票。
- 分類 (c) の画面デモ 6 本 (`asset_demo` `blit_test2` `demo_tile` `gfx200_test` `gfx_demo200` `rotate_test`)。
- 分類 (e) の対話・キー待ち 5 本 (`kbd_echo` `mouse_test`、末尾 `kbd_getchar()` でブロックする
  `blit_test` `gfx200_test` `tile_bench`)。自動回帰に載せるには `--batch` 相当が要る = 別の判断。
- 分類 (b) の計測・ベンチ (`cfg_bench` `kstr_bench` など)。**合否ではなく数字を出すのが目的**なので、
  約束事の対象外とする。ただし異常時の終了コードは 2-1 に従う。
- ランナー本体 (`runtests` / `tools/guest_tests.py` / `make check-guest`) = 3 段目。
- `hal_test` が合否を**色でしか区別しない**問題 (文字列は両方 `hal_test done`)。第 2 陣で扱う。

---

## 9. ゲスト受入 (PM、2026-09-17、`e420f37` + 本節の修正)

NP21/W の既定構成、`make programs` → `make deploy` → ゲストの `hsync`。
実行は `.claude/skills/run-os32/driver.py cmd --wait`。

### T5 — `ring3_hello` が戻る

**合格。** 約 5 秒で戻り `$?` = 0。修正前はスロット 0 (`gfx_init`) を呼んで
無限ループに落ちる作りだったので、**戻ってくること自体が修正の証拠**。

### T2 / T3 — 第 1 陣の `$?` と集計行

| 試験 | 集計行 | `$?` |
|---|---|---|
| `klibc_test` | `klibc_test: PASS 49/49` | 0 |
| `math_test` | `math_test: PASS 110/110` | 0 |
| `mgx_test` | `mgx_test: PASS 76/76` | 0 |
| `ecs_test` | `ecs_test: PASS 45/45` | 0 |
| `asset_test` | `asset_test: PASS 23/23` | 0 |
| `gui_call_test` | `gui_call_test: PASS 2/2` | 0 |
| `input_test` | `input_test: PASS 29/29` | 0 |
| `db_v50_test` | `db_v50_test: PASS 41/41` | 0 |
| `save_test` | `save_test: PASS 15/15` | 0 |
| `font_load_test` | `font_load_test: PASS 1/1` | 0 |
| `test2` | `test2: PASS 5/5` | 0 |
| `stat_t` | `stat_t: PASS 5/5` | 0 |
| `restest all` | `restest: PASS 3/3` | 0 |
| `e2test` | `e2test: SKIP cannot allocate the 372KB buffers` | 2 |
| `host_test` | SKIP (ホスト側の Agent が走っていない) | 2 |
| `db_test` | **出ない (落ちた)** | **139** |

**集計行と `$?` は全件一致。** `139` は例外で畳んだときの予約値なので、
`db_test` が落ちたことが**値だけで分かる** — 約束事が働いている証拠。

### 約束事を入れた途端に露見した不具合 3 件

いずれも**終了コードが常に 0 だったので誰も気づいていなかった**もの。

1. **`stat_t` が `HELLO.BIN` を見ていた** — FAT 時代の名残で今のルートに無い。
   FAIL 4/5。起動していれば必ず在る `/bin/sh.bin` に変えて PASS 5/5。
   これが stat できないなら環境の破損なので、SKIP ではなく**不合格のまま**にした
   (飛ばすと「ここでは関係ない」に見えて破損が隠れる)。
2. **`restest` が `/shell` を開いていた** — 同じく古い配置の名残。3 本とも
   `-2` (`OS32_ERR_NOTFOUND`) で失敗していた。FAIL 2/3 → PASS 3/3。
3. **`db_test` が `db_last_error()` で落ちる** — 下の §10。

### T6 — `/host` が無い構成

**未実施。** HostDrv を持たない構成をこの環境で作れていない
(`hostdrvfs_detect()` が真になる NP21/W でしか動かしていない)。
第 1 陣は詳細ファイルを書かないので影響は無いが、**確かめていない** ([V4])。

---

## 10. 見つけたカーネル層の不具合 — `db_last_error()` は CPL=3 から使えない

`kapi/kapi_db.c:727` の `kapi_db_last_error()` は `sqlite3_errmsg(slot->db)` を
**そのまま返す**。SQLite はカーネル側 (0x200000〜0x2FFFFF) に居るので、
返るのはカーネル番地のポインタで、**CPL=3 のアプリが読むと #PF で死ぬ**。

実測 (2026-09-17、`db_test`):

    [ring3] #PF (CPL=3 / syscall) addr=0x002B8DE0 EIP=0x005005C7 -> kill app

`0x2B8DE0` は SQLite の帯の中。早期 return の `"invalid handle"` もカーネルの
`.rodata` なので、**この関数の戻り値はどれも CPL=3 から読めない**。

効き方が悪い。**`CLAUDE.md` は「枯渇の診断は `db_last_error()` を必ず出す」と
書いており** (`docs/POLICY_DEBUG.md` §4-13 も同じ)、実機で DB を調べる唯一の道具
`userland/tests/dbq.c:19` がエラー時にこれを呼ぶ。**いちばん必要なときに落ちる。**

正しい口は SHM 経由の `db_errmsg()` (`userland/tests/db_v50_test.c:39` の注記)。

**カーネル層なので POLICY_DEV §1 により新機能より先。別票を立てる。**
本票では直さない (KAPI の戻り方を変える話で、[ABI2] と版数が絡む)。

---

## 11. 追補 — 集計行は **fd 1 へ出す** (2026-09-17、ランナーが暴いた)

段 3 のランナー ([`TASK_TEST_RUNNER.md`](TASK_TEST_RUNNER.md)) を実機で初めて回したら、
**16 本中 14 本で「集計行が無い」**と報告された。

    klibc_test      PASS       0     klibc_test: PASS 49/49
    math_test       MISMATCH   0     $?=0 なのに集計行が無い
    ...
    合計 16 件: PASS=2, MISMATCH=14

原因は出力先。**通った 2 本は `printf`、落ちた 14 本は `api->kprintf`** を使っていた。

| 出す口 | 行き先 | `>>` で拾えるか |
|---|---|---|
| `printf` (newlib) | fd 1 | **拾える** |
| `api->kprintf` | カーネルの画面出力 | **拾えない** |

`kprintf` は画面に直接書くのでリダイレクトを素通りする。**人間には見えていたので
誰も気づかなかった** — 機械で集めようとした瞬間に初めて露見した。

### これは §2-2 の書き漏らしである

§2-2 は「最終行に 1 行」としか書いておらず、**どの口へ出すかを決めていなかった**。
`userland/lib/rt/testresult.h` の使用例が `api->kprintf` を勧めていたのも効いている。

### 決め直す

**集計行は `sys_write(1, …)` で出す。**

- `printf` は newlib を要するので全部には使えない (`-nostdlib` の試験がある)。
- `kprintf` は画面に行くのでリダイレクトを通らない。
- `sys_write(1, …)` は libc に依存せず、**リダイレクトも通る**。

`testresult.h` に行を出すところまで持たせる (今は行を組み立てて返すだけ)。
**短い書き込みを「書けた」ことにしない。**

### 受入

| ID | 反例・操作 | 期待 |
|---|---|---|
| T7 | 第 1 陣を `> file` にリダイレクトして実行 | **16 本すべて**の集計行がファイルに落ちる |
| T8 | ホスト試験 | 集計行が **fd 1 に出る**ことを見る。`kprintf` に戻す変異で RED |
| T9 | `make check-guest` | 食い違い 0 件 |
