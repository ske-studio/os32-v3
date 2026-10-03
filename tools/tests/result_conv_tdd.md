# 試験の合否を機械が読める形にする約束事 (ホスト試験の RED→GREEN の記録)

- 票: [`docs/archive/test/TASK_TEST_RESULT.md`](../../docs/archive/test/TASK_TEST_RESULT.md)
  §2 (約束事の制定) / §4 (第 1 陣) / §5 (欠陥 2 件) / §6 の段取り 1〜4 /
  **§11 (追補 — 集計行は fd 1 へ。下の §7)**
- 実行: `python3 -B tools/tests/test_result_conv.py [--target] [--mutate]`
  (`make check-result-conv-host` が `--target --mutate` 付きで回す)
- 対象:
  - `userland/lib/rt/testresult.h` — 約束事の**唯一の管理元** (新設)
  - `userland/tests/` の第 1 陣 16 本 (実物 5 本はハーネスが**走らせる**)
  - `userland/rust/alloc_demo/src/lib.rs` — 書式と終了コードを突き合わせる
- ハーネス: `tools/tests/test_result_conv_host.c` +
  `tools/tests/result_conv/run_*.c` (実物の取り込み)

## 0. 正直に書く ([V4])

- **ゲスト / エミュレータでは 1 行も動かしていない。** 受入 T2 / T3 / T5 / T6
  (実機で `$?` を見る、`ring3_hello` が戻ってくる、`/host` 無しの構成) は
  **未実施**。票 §7 のとおり PM の担当。
- ホストで走らせたのは 5 本 (`stat_t` / `restest` / `test2` / `klibc_test` /
  `font_load_test`)。残り 11 本は**静的検査とクロスコンパイルだけ**で、
  実際に走らせてはいない。
- `gui_call_test` は**ホストでは走らせられない** — `os32_gui_shared.h` の
  `STATIC_ASSERT` がポインタ幅 4 を前提にしていて、64 ビットのホストでは
  構造体の大きさが合わず組めない。このホストには gcc-multilib が無いので
  `-m32` も使えなかった。静的検査とクロスコンパイルだけが掛かっている。

## 1. 何を確かめる試験か

ランナー (3 段目) が人の目を使わずに合否を判定するには、次の 4 つが要る。

| | |
|---|---|
| 終了コード | 0 = 全部合格 / 1 = 1 件以上不合格 / 2 = 実行しなかった。**126 / 127 / 130 / 139 は返さない** (シェルの `$?` の予約値と衝突する) |
| 集計行 | 最終行に 1 行、`<名前>: PASS <n>/<m>` / `FAIL <n>/<m>` / `SKIP <理由>` |
| **一致** | 上の 2 つが必ず同じことを言う。片方だけ直すとランナーが 2 つの答えを持つ |
| **出し口** | 集計行が **fd 1** に出る。`kprintf` は画面へ直に書くのでリダイレクトを素通りする (票 §11、下の §7) |

3 つ目が肝で、**grep では確かめられない**。「`FAIL` と出しながら 0 を返す」版も
「`PASS` と出しながら 1 を返す」版も、集計行の grep はどちらも通してしまう。
そこでこの試験は**実物のプログラムを贋物の KernelAPI で走らせ、出た文字列と
`main` の返り値の両方を同じ場面で観測する**。

## 2. 作り

### 2-1. 約束事を 1 か所に置いた

`userland/lib/rt/testresult.h` が終了コードの定数・予約値の表・集計行の組み立てを
持つ。`os32_test_summary()` は**行を書くのと終了コードを返すのを 1 回の呼び出しで
行う**ので、片方だけ直すことが構造的にできない。

    rc = os32_test_summary(line, sizeof(line), "math_test", g_passed, g_total);
    api->kprintf(rc ? ATTR_RED : ATTR_GREEN, "%s", line);
    return rc;

出力先はヘッダで決めていない。`kprintf` を使う試験と newlib の `printf` を使う
試験が混在しているので、**行を組み立てて終了コードを返すところまで**を持つ。

### 2-2. 実行時 (ハーネス §1〜§5)

- §1〜§4 … 実物のヘッダを直に叩く。書式、値域、予約値、切り詰め。
- §5 … 実物の 5 本を走らせる。合格側・不合格側・SKIP 側の 3 通りを同じ 1 本で
  踏み、**`argv[0]` を `stat_t` と `/usr/bin/stat_t.bin` の 2 通りにして
  集計行が同じであること**も見る (名前が `argv[0]` 由来だと、リダイレクト先や
  呼び方で行が変わってランナーが取りこぼす)。

取り込みは `tools/tests/result_conv/run_*.c` が `#define main <名前>_main` +
`#include` で行う。**プログラム本体は 1 行も写していない。** 呼ぶ側の宣言は
わざと別の翻訳単位 (ハーネス) に置いてある — 同じ .c に置くと `void main` に
戻す変異がコンパイルエラーになってしまい、試験の目が働いたことにならない。

### 2-3. 静的 (ハーネスとは別に Python 側)

第 1 陣 16 本 + `rt/testresult.h` を取り込んだ試験すべてについて:

- `void main` が残っていないこと、`main` が `KernelAPI *` を受け取ること
- 集計行の名前が**固定文字列**で、プログラム名と同じこと
- `main` の `return` が**必ず** `os32_test_summary*` の答えであること
  (`return 0;` を書いた瞬間に落ちる)
- 予約値 126 / 127 / 130 / 139 を返す経路が無いこと

Rust の `alloc_demo` は C のヘッダを使えないので、書式・`PASS`/`FAIL` の語・
終了コードの定数を `rt/testresult.h` から読んだ値と突き合わせる。

## 3. RED → GREEN

### 3-1. 手を入れる前の RED (2026-09-17 に実測)

`git show HEAD:userland/tests/<名前>.c` で**票を起こした時点の 16 本**を取り出し、
静的検査だけを掛けた (ハーネスの実行時側は、`stat_t` が `api->sys_exit(0)` で
戻らない・`klibc_test` / `font_load_test` の署名が `KernelAPI *` を取らないので
そもそも組み上がらない)。

    TOTAL violations on the original sources: 21

      klibc_test: main が KernelAPI * を受け取っていない: (int argc, char **argv)
      font_load_test: main が KernelAPI * を受け取っていない: (int argc, char **argv)
      asset_test:     `void main` — 終了コードが eax の残骸になる
      gui_call_test:  `void main` — 終了コードが eax の残骸になる
      test2:          `void main` — 終了コードが eax の残骸になる
      (16 本すべて) rt/testresult.h を取り込んでいない

検査は「管理元を取り込んでいない」を見つけた時点でその 1 本を打ち切るので、
**この 21 件には `return 0;` (集計を終了コードに載せていない 8 本) や
`font_load_test` の負値は数えられていない** — 取り込みを足した後に順に出てくる。
数を比べる記録ではなく、「どこから RED だったか」の記録として読むこと。

### 3-2. GREEN (2026-09-17)

    157 checks, 0 failed
    EXIT test_result_conv_host=0
    STATIC 16 sources, 0 violations
    RUST alloc_demo: 0 violations
    TARGET i386-elf -Werror COMPILE PASS (16 sources)
    TARGET i386-elf -Werror COMPILE PASS (userland/tests/ring3_hello.c)
    TARGET i386-elf -Werror COMPILE PASS (userland/tests/ring3_fault.c)
    TARGET i386-elf -Werror COMPILE PASS (userland/tests/ring3_guard.c)

## 4. 否定側 (`--mutate`)

**11 本すべてコンパイルは通る**変異にしてある。コンパイルエラーで落ちるだけなら
試験の目が働いたことにならないので、`--mutate` は「組めなかった」を
`GREEN のまま` と同じく**不合格**として数える。

| # | 変異 | 結果 |
|---|---|---|
| 1 | 集計行を `kprintf` に戻す (画面には出るが fd 1 に出ない) | RED 46 件 |
| 2 | 短い書き込みを「書けた」ことにする | RED 20 件 |
| 3 | 行と終了コードを別々の呼び出しで作る (`os32_test__build` + 自前の出力) | RED 20 件 |
| 4 | 出せなかったのに元の終了コードを返す | RED 23 件 |
| 5 | `font_load_test` の SKIP が予約値 127 を返す | RED 22 件 |
| 6 | `asset_test` を `void main` に戻す | RED 1 件 (静的) |
| 7 | `restest` の集計行の名前を `argv[0]` 由来にする | RED 19 件 |
| 8 | 約束事の本体: 総数 0 を合格にする | RED 18 件 |
| 9 | 約束事の本体: 終了コードを 0 に固定する (行は FAIL のまま) | RED 30 件 |
| 10 | 約束事の本体: SKIP を 1 にする (前提の欠如と不合格が混ざる) | RED 28 件 |
| 11 | `alloc_demo` の `sum == 285` を比較しない旧版に戻す | RED 1 件 |

変異 7 が実行時に落ちるのは、同じ場面を `argv[0] = "restest"` と
`argv[0] = "/usr/bin/restest.bin"` の 2 通りで回して集計行を突き合わせているため。

変異 1〜4 は票 §11 の否定側。**贋物が「画面に出た」と「fd 1 に出た」を
区別できないと 1 本も落ちない** — 1 と 3 は kprintf に出しており、画面と fd 1 を
同じバッファへ流す贋物では GREEN のまま通ってしまう。

## 5. 直した欠陥 (票 §5 と、作業中に出たもの)

| 場所 | 欠陥 |
|---|---|
| `userland/rust/alloc_demo/src/lib.rs` | **何も検査していなかった**。常に 0 を返し、最後に無条件で `All tests passed!` を印字。`sum=285` はコメントだけ。回帰台本に偽の合格が 1 件混ざっていた (票 §5-1) |
| `userland/tests/ring3_fault.c` / `ring3_hello.c` | `int 0x80` を `eax=0` で呼んでいた = スロット 0 = `gfx_init`。終了せず無限ループになる。`ring3_guard.c` で 2026-09-06 に直った修正から漏れていた (票 §5-2) |
| `userland/tests/test2.c` | `sys_write` は**書けたバイト数**を返す (`fs/vfs_fd.c`) のに `wr == 0` を成功と読んでいた。11 バイト書けても `FAIL` を出していた (票に無い。作業中に発見) |
| `userland/tests/save_test.c` | `/host` 決め打ち 9 か所。HostDrv の無い構成では全滅していた → 帯域を 1 か所にして `sys_is_mounted` で SKIP (票 §4) |
| `userland/tests/klibc_test.c` | 集計カウンタを `main` で初期化していないので 2 回目以降の呼び出しで数が積み上がる (票に無い。ハーネスが 2 回走らせて発見) |
| `userland/tests/save_test.c` | `test_peek()` が文の後ろで宣言していた ([C1] 違反。`-Werror` を付けたら落ちる) |

## 6. この試験がまだ見ていないこと

- **ゲストでの実行**。終了コードがシェルの `$?` まで実際に届くことは
  `tools/tests/test_sh_status.py` (前段) が見ているが、**この 16 本を繋いだ
  確認は誰もしていない**。
- 第 1 陣の外の 28 本。票 §8 のとおり次の票。
- `hal_test` が合否を色でしか区別しない問題 (第 2 陣)。
- 詳細ファイル `/host/test/<名前>.txt` (票 §2-3)。**まだ誰も書いていない** —
  任意なので第 1 陣では作らなかった。

---

## 7. 追補 (2026-09-17) — 集計行は **fd 1** へ (票 §11)

### 7-1. 何が起きたか

段 3 のランナーを実機で初めて回したら **16 本中 14 本で「集計行が無い」**。
通った 2 本 (`klibc_test` / `font_load_test`) は `printf`、落ちた 14 本は
`api->kprintf` を使っていた。`kprintf` はカーネルの画面出力なので
**リダイレクトを素通りする** — 人間には見えていたので誰も気づかず、機械で
集めようとした瞬間に初めて露見した。

**この試験は GREEN のままだった。** 原因は贋物の側にある。2026-09-17 の初版は
`kprintf` も `printf` も**同じ 1 本の捕捉バッファ**へ流していたので、「画面に
出た」と「fd 1 に出た」の区別が付かなかった。約束事の側も §2-2 が「最終行に
1 行」としか書いておらず、出し口を決めていなかった。

### 7-2. 直し方

**`rt/testresult.h` が出すところまで持つ。**

- 公開は `os32_test_summary(api, name, pass, total)` /
  `os32_test_summary_skip(api, name, reason)` の 2 本だけ。
  **呼び手は行のバッファを受け取らない** — 受け取れた瞬間に出し口が
  呼び手任せに戻るからで、それが今回の原因だった。
- 出すのは `api->sys_write(1, …)`。`printf` は newlib を要するので
  `-nostdlib` の試験に使えず、`kprintf` はリダイレクトを通らない。
  **両方を満たすのはこれだけ。**
- 短い書き込みは書けたバイト数だけ進めて**残りを書き続ける**。1 バイトも
  進まなくなったら諦め、**終了コードを FAIL (1) にする** — 出せなかった
  集計行はランナーには存在しないので、0 を返すと票 §11 の穴が形を変えて
  戻ってくる。1 にしておけば「`$?`=1 なのに集計行が無い」= 食い違いとして
  ランナーが必ず名指しする ([V4])。
- 切り詰めても**改行だけは落とさない**。落ちるとランナーが次の出力と 1 行に
  繋げて読むので、行が丸ごと消えるのと同じになる。

第 1 陣 16 本は全部この 1 本に揃えた。`printf` だった 2 本も同じ口にした
(2 通りあると次に触る人が迷う)。集計行の `kprintf` は**消した** — fd 1 は
リダイレクトされていなければ画面に出るので、残すと同じ行が 2 回見える。
集計行**以外**の出力 (各項目の `[OK]` / `[FAIL]` など) は今までどおり。

### 7-3. 贋物を作り直した

行き先ごとに別のバッファを持たせた。**ここを分けないと同じ穴をまた見逃す。**

| 贋物のバッファ | 何が入るか | `>` で拾えるか |
|---|---|---|
| `sk_scr` | `api->kprintf` | **拾えない** |
| `sk_fd1` | `sys_write(1, …)` と `printf` | 拾える |
| `sk_other` | `sys_write(その他の fd)` | — |

さらに `sk_fd1` は**最後に `sys_write` で書き始めた位置**を覚えていて、集計行が
そこから始まっていることまで見る (`printf` 任せに戻す変更も拾う)。
`test2` の「短い書き込み」の場面は **fd 1 以外**にだけ効かせている — 混ぜると
「集計行が出ない」のか「ファイル I/O が落ちた」のか区別できなくなる。

### 7-4. 結果

    194 checks, 0 failed
    EXIT test_result_conv_host=0
    STATIC 16 sources, 0 violations
    RUST alloc_demo: 0 violations
    HEADER rt/testresult.h: 0 violations
    TARGET i386-elf -Werror COMPILE PASS (16 sources)
    MUTATE 1〜11 すべて RED

`make CROSS_DIR=... programs` も通る (16 本のリンクまで確認)。

### 7-5. 確かめていないこと ([V4])

- **ゲストでは 1 行も動かしていない。** 受入 T7 (`> file` で 16 本すべての
  集計行が落ちる) と T9 (`make check-guest` の食い違い 0 件) は**未実施**。
- `restest redirect` は fd 1 をファイルへ向けたまま終わるのが試験の中身なので、
  集計行もそのファイルへ落ちる。**これは正しい**が (集計行はリダイレクトに
  従う行)、自動回帰に載っているのは `restest all` (リダイレクト試験を含まない)
  なので実害はない — と**机上で判断しただけで、ゲストで確かめていない**。
- Rust の `alloc_demo` は**まだ `kprintf` で出している**。第 2 陣で足すときに
  同じ穴を踏むので、そのときに `sys_write(1, …)` へ揃えること。

## T2e e8b 更新 (2026-10-03)

`font_load_test` の正常対照は boot 後の `OS32_ERR_NOSYS` で PASS 1/1。
0 (boot 後の読込成功) と -3 は FAIL 0/1、stat 失敗は従来どおり SKIP。
変異5は新しい SKIP 理由に当て直し、予約終了値127を返す誤りの検出を維持。
既存11変異は全て RED。表・BB・mailbox は
[kcg_boot_tdd.md](kcg_boot_tdd.md) の実ドライバ試験で照合する。
