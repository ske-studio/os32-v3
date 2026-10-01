# c_dialect (言語モードの検査、[C1]) — ホスト TDD の記録

票: [docs/tasks/v3/TASK_C11_MIGRATION.md](../../docs/tasks/v3/TASK_C11_MIGRATION.md) §6 段 4・段 5、受入 A1・A2・A5・A6
実装: `tools/check_c_dialect.py` (`make check-c-dialect`)
試験: `tools/tests/test_c_dialect.py` (`make check-c-dialect-host`、否定側は `--mutate`)

## 様式

検査器は旗の**文字列**でなく**コンパイル結果**で判定する。

**位置づけ**: 誤って持ち込むことを防ぐ guard であり、わざと作った入力への耐性は目標にしない (ユーザー決定 2026-09-30、Codex レビュー 3 回目の後)。既知の限界は末尾の「見ていないもの」。

1. `make -n -B all` のコンパイル行 (`i386-elf-gcc … -c`) を読み、旗の組 (−I・依存生成・`-c`・`-o`・ソースを除いたもの) ごとに
   `-E -dM` で `__STDC_VERSION__` / `__STRICT_ANSI__` を聞く。期待は SQLite 系 (`lib/sqlite3/{sqlite3,os32_sqlite_vfs,os32_sqlite_test}.c`、
   `userland/tests/sqlite_standalone/`) が gnu89、それ以外が gnu11。SQLite 系・`kernel/kernel.c`・`boot/boot_main.c` がコンパイル行に無ければ落ちる (空振りの番人)。
2. gnu11 の旗の組ごとに探りを 5 本コンパイルする: VLA・暗黙の関数宣言・暗黙 int・偽の `STATIC_ASSERT` は**期待する診断の文言つきで**拒否、
   真の `STATIC_ASSERT` は通る。`STATIC_ASSERT` は木の `include/types.h` の実物のマクロを使う。
3. 公開 SDK ヘッダ (`sdk/include/os32/*.h`。`os32_kapi_shared.h` もここ) は `-std=gnu89 -Wc90-c99-compat -Wc99-c11-compat
   -Wdeclaration-after-statement -Wlong-long -Werror` と `-std=gnu11 -Werror` の両方で取り込む (行コメント・宣言位置・`_Bool`・
   指示付き初期化子・`restrict`・`_Static_assert`・`long long` はコンパイラが拒否する)。加えて前処理後の出力 (下の 4 と同じ読み方) で、
   SDK ヘッダが取り込む `<stdbool.h>` ほかと、SDK ヘッダ自身の行に残る C99/C11 の語を拒否する。前処理は `-dD` で `#define` を
   出力に残し、この木では展開されないマクロの置換列も見る (外部アプリが展開すれば到達するため)。
   SDK が配るライブラリヘッダ (`build/sdk.mk` の `SDK_LIB_HEADER_DIRS`・`rt`・`lib/utf8.h`) は gnu89 と gnu11 で取り込めること
   (`rt/dbgserial.h` の可変引数マクロは C99 の機能だが gnu89 の GNU 拡張で通るので、ここは「取り込める」までを見る)。
   `sdk/example/hello` は Makefile の `-std` が gnu89 のままで、in-tree の SDK ヘッダで gnu89 のままコンパイルできること
   (apps/game は組まない — ユーザー決定。A5 の代わりの確認)。
4. 内部実装: 1 の翻訳単位のうち vendor (`lib/sqlite3 lib/zlib lib/microtar lib/fatfs lib/os32_lz4 fs/fatfs userland/rust`) の元ソースと
   生成物 (`BUILD_OUT` の `build_id.c` など、木に無いソース) を除いたもの (329 単位) を、**その単位の実際の引数で `-E -w`** に通す。
   行継続 (バックスラッシュと改行の間の空白・タブを含む)・コメント・VT/FF・`#if`・マクロ・`#include` の解釈は本物の前処理器に任せ、
   自前は前処理後の各行から文字列・文字定数を除いて識別子を数えるだけ。ファイルと行は行標識 (`# N "file" flags`) から取り、
   禁止ヘッダ (`stdatomic.h` `threads.h`) は行標識の「入った」(flag 1) と「戻った」(flag 2) で「どのファイルの何行目の #include か」を得る
   (戻り先の行番号 − 1)。禁止語は `_Atomic` `_Thread_local` `__thread` `restrict`。木の外 (システムヘッダ) と vendor のファイルは数えない。

`make`・エミュレータ・配備は使わない (要るのはクロスコンパイラと `make -n` だけ)。
`make -n` への引き継ぎ: 親の `MAKEFLAGS` から並列 make の引き継ぎ (`-j…`、`--jobs…`、`--jobserver-auth=…`、`--jobserver-fds=…`) だけを
除き、残り (単文字旗の束の `e`、` -- ` 以降の変数指定) は**バイト列のまま**子の環境の `MAKEFLAGS` に渡す (エスケープの解釈は子の make に任せる)。
`BUILD_OUT` はコマンドラインで一時ディレクトリに向ける (`build/config.mk` の `$(shell mkdir -p …)` は `-n` でも走るため。
コマンドラインの指定が `MAKEFLAGS` から来た指定に勝つので、利用者の `BUILD_OUT` は使わない)。ビルドの旗を変える保存設定
(`build/*.mk` が `$(wildcard)` / `$(shell cat)` で `BUILD_OUT` から読むもの。確かめた範囲では `lgy98.flags` だけ) は実物の `build/out` から一時の
`BUILD_OUT` に写してから `make -n` する (`make kernel-lgy98` の後は LAN の `-D` も実際の組で読む)。

## 試験の区分 (81 チェック)

| 区分 | 何を固定したか |
|---|---|
| 1 字句 | 本物の前処理器の出力で: コメント・文字列・`__restrict` を数えない、文字定数 `'"'` で文字列に入らない、`#include /* … */ <x.h>`、複数行コメントの後ろの `#include` は `#` の行、行継続 (素・空白・タブ入り) で割った `restrict`、`# include`・`# include`、行継続で割った `#include` (行は指令の終わりの物理行)、マクロで隠した `_Atomic`、`#if 0` の中は数えない |
| 2 コンパイル行 | `i386-elf-gcc -c` の行だけ拾う (`-o` が先でも)、旗の組から −I・依存生成・`-c`・`-o` を外し `-include` は残す、期待する言語モード (SQLite 系 4 種は gnu89、本体・ブート・userland は gnu11) |
| 3 実際の言語モード | `strip_jobserver`: `-j` / jobserver だけを除き、変数部分と `e` などはバイト列のまま。`-std=gnu11`/`gnu89`/`c11`、`-std` 2 つは後ろが効く、`-std` 無しはコンパイラの既定 (gnu17) で gnu11 とは読まない |
| 4 拒否の探り | 旗が揃えば全部拒否、`-Werror=vla`・`-Werror=implicit-function-declaration`・`-Werror=implicit-int` をそれぞれ外すとその探りが通る、別の理由 (壊れた `-include`) の失敗を拒否と数えない、条件を捨てる `STATIC_ASSERT` を見逃さない、負サイズ配列の `STATIC_ASSERT` でも偽は拒否、マクロが無ければ真の探りが通らない |
| 5 公開 SDK ヘッダ | C89 の書き方だけのヘッダは通る (文字列の `//` 含む)。行コメント・ブロック途中の宣言・`for` の中の宣言・`_Bool`・`<stdbool.h>`・指示付き初期化子・`_Static_assert`・`restrict`・`long long` をそれぞれ拒否。ヘッダが 1 本も無ければ落ちる  行継続で割った `//`、`#\f include <stdbool.h>` も拒否 (コンパイラと前処理後の出力で) |
| 6 内部実装 | 翻訳単位を実際の旗で前処理: C11 で許す書き方・コメントと文字列の中・vendor の元ソースと vendor のヘッダは数えない。`_Atomic`・`_Thread_local`・`restrict`・`<stdatomic.h>`・`<threads.h>` (前処理で落ちる)・Codex の反例 (コメント入り include、行継続 3 種、VT/FF、複数行コメントの後ろの include は 3 行目)・取り込んだ内部ヘッダの中 (`kernel/b.h:1`) を拒否 |
| 7 実物の木 | 実物の木で rc=0、要約に gnu11 と gnu89 の単位数。本物の `make` 経由で: `C_STD=-std=gnu89`・`C_STD=-std=gnu89 make -e`・`C_STD=-std=gnu89 'X=foo\'`・`'X=foo\' C_STD=…`・`'C_STD=$(MODE)' MODE=-std=gnu89` は落ち、`'C_STD=$(MODE)' MODE=-std=gnu11`・タブ入りの値 (`T0_TAB`、`C_STD=-std=gnu11<TAB>`)・`-j4` は通る |

## RED → GREEN

### R0 — 検査器が無い (2026-09-30)

段 4 の試験を先に書き、`tools/check_c_dialect.py` が無い状態で回した:

```
$ python3 -B tools/tests/test_c_dialect.py
FileNotFoundError: [Errno 2] No such file or directory: '…/tools/check_c_dialect.py'
```

実装後: `53 checks, 0 failed` / `PASS`。

### 検査器の誤りを試験が捕まえるか (1 回目の実装の自前の字句に対するもの。K1 の対象は 2 回目で撤去) (写しの木で検査器だけを差し替えて試験を回す)

実物の `tools/check_c_dialect.py` は書き換えず、`mutpar.mutant_tree` の写しに差し替えた検査器を置いて
写しの `tools/tests/test_c_dialect.py` を回した (手作業、2026-09-30)。

| # | 差し替え | 落ちたケース |
|---|---|---|
| K1 | 字句で引用符を文字列の始まりと見ない | 「文字列の中の // は行コメントでない」ほか 4 件 (C89 だけのヘッダまで拒否) |
| K2 | 探りの拒否を rc だけで判定 (診断の文言を見ない) | **最初は生き残った** → 区分 4 に「別の理由 (壊れた `-include`) の失敗を拒否と数えない」を足して RED |
| K3 | 言語モードを旗の文字列の最初の `-std` で読む | 「`-std` を 2 つ並べると後ろが効く」 |
| K4 | 公開 SDK ヘッダを gnu11 だけで取り込む | 「`for` の中の宣言」「指示付き初期化子」を混ぜたヘッダを拒否する |
| K5 | vendor の除外を外す | 「vendor は数えない」 |
| K6 | userland の SQLite 単体を gnu89 の期待から外す | 「userland の SQLite 単体は gnu89」「実物の木で rc=0」 |

### Codex レビュー 1 回目の偽合格 2 件 (2026-09-30、P2-1・P2-2)

(ここで入れた `make_overrides()`・`_splice()`/`_lex()` は 2 回目の指摘で撤去し、本物の make と前処理器に任せる形に置き換えた — 下の節。)

- **P2-1** — `dry_run()` が `MAKEFLAGS` を丸ごと捨てていたので、`make check-c-dialect C_STD=-std=gnu89` で
  実際の `make -n` は kernel.c を gnu89 で組むのに検査器は OK。直し: `make_overrides()` で `--` の後ろの変数指定だけを
  子の make に渡す。修正前の検査器 (`git show HEAD:…` の写し) で `MAKEFLAGS=" -- C_STD=-std=gnu89"` → `check_c_dialect: OK`
  rc=0、修正後 → `337 単位が gnu11 でなく gnu89` rc=1。`make check-c-dialect C_STD=-std=gnu89` は rc=2 (make の失敗)。
- **P2-2** — ヘッダ名をコメント除去前の行から読み、行継続を残していた。反例 (a) `#include /* C11 */ <stdatomic.h>` +
  `atomic_int`、(b) `re\` 改行 `strict` が通った。直し: `_splice()` (翻訳段階 2) → `_lex()` (コメントを空白、改行も
  空白にして論理行を割らない) → 判定。行番号は各文字の元の行を持ち回る。
- 試験を先に足して修正前の検査器で回した結果 (写しの木): 字句の新ケース 5 件が FAIL、`make_overrides` が無く
  AttributeError。変異 20・21 (反例 a・b) は修正前の検査器で **UNEXPECTED GREEN** (`MUTATIONS 19/21 RED`)、
  修正後は RED。

### Codex レビュー 2 回目 (2026-09-30) — make と C の字句の自作をやめる

1 回目の直しの変形で P2 が 4 件 (`make -e` + 環境の `C_STD` で子が `-e` を失う、`make_overrides()` のエスケープ復元の穴 3 種、
バックスラッシュと改行の間に空白のある行継続、`#\vinclude` / `#\finclude`) と P3 (複数行コメントの後ろの `#include` の行番号)。
継ぎ足しをやめ、`MAKEFLAGS` は `-j` / jobserver だけを除いてバイト列のまま渡し、字句は本物の前処理器 (`-E`) に任せた
(`-fpreprocessed -E` はコメントを消すが行継続を結合しない、`-fpreprocessed -fdirectives-only -E` は結合するが既定のマクロ無しで `#if` を
評価してしまう — 手元で確かめて不採用。翻訳単位の実際の引数での `-E` なら両方ともコンパイラと一致する)。

修正前の検査器 (`git show HEAD:…` を写しの木に置いたもの) と修正後の実測:

| 反例 | 修正前 | 修正後 |
|---|---|---|
| `C_STD=-std=gnu89 make -e check-c-dialect` | `check_c_dialect: OK` rc=0 (偽合格) | rc=2 (NG gnu89) |
| `make check-c-dialect C_STD=-std=gnu89 'X=foo\'` | `check_c_dialect: OK` rc=0 (偽合格) | rc=2 (NG 337 単位が gnu89) |
| `make check-c-dialect 'C_STD=$(MODE)' MODE=-std=gnu11` | rc=2 (偽不合格) | rc=0 |
| `make check-c-dialect 'C_STD=-std=gnu11<TAB>'` | rc=2 (`-std=gnu11\t` を gcc が拒否、偽不合格) | rc=0 |
| 変異 22 (`re\ ` 改行 `strict`)・23 (`#\vinclude`)・24 (`#\finclude`) | **UNEXPECTED GREEN** (`MUTATIONS 22/25 RED`) | RED |
| 変異 25 (複数行コメントの後ろの `#include`) | RED (行番号は先行コメントの開始行) | RED、行は `#` の行 (区分 1・6 で固定) |

### Codex レビュー 3 回目 (2026-09-30) — 直した 2 件

往復の上限に達し、ユーザー決定で 5 件のうち 2 件を直し、3 件は「見ていないもの」に記録した。

- **(1) 公開 SDK ヘッダの展開されないマクロ** — `#define OS32_ASSERT(x) _Static_assert(x, "x")` は gnu89 / gnu11 の取り込みも
  前処理後の本文も通った。直し: SDK ヘッダの前処理に `-dD` (`#define` を行標識つきで出力に残す。行番号が元のヘッダと一致し、
  置換列の文字列は他の行と同じく除けることを小さな入力で確認)。
- **(5) 保存済みの LAN 設定** — `lgy98.flags=5` のとき `#if CONFIG_LGY98_FLAGS == 5` の中の `_Atomic` は、一時の `BUILD_OUT` が空で
  既定値 0 で読まれて見逃した。直し: `SAVED_SETTINGS` (`lgy98.flags`) を一時の `BUILD_OUT` に写す。`build/*.mk` の `$(BUILD_OUT)` を
  読む `$(wildcard)` / `$(shell cat)` / `include` を grep し、ほかに無いことを確認。
- 修正前の検査器 (写しの木): 変異 25 (SDK のマクロ)・26 (`lgy98.flags=5` + `#if` の中の `_Atomic`) が **UNEXPECTED GREEN**
  (`MUTATIONS 25/27 RED`)。修正後は RED。対照 30 (`lgy98.flags=0` なら同じ `#if` は組まれない)・31 (置換列の文字列の中の語) は GREEN。

## 否定側 (`--mutate`、段 5) — 実物の木の写しに変異を当てて検査器を回す

`tools/tests/mutpar.py` の写しの木で、変異は写しにだけ当てる (実物は書き換えない)。C11 で許す書き方
(`//`、ブロック途中の宣言) を内部実装に足す変異は**対照**で、通る (GREEN) のが期待。
コンパイル拒否を期待するのは検査器の探りで、通常の実行変異試験 (コンパイル失敗 = 試験不成立) とは別。

```
$ OS32_MUT_JOBS=4 python3 -B tools/tests/test_c_dialect.py --mutate
MUTATION 1 RED: 本体から -Werror=vla を外す (VLA が通る) -- check_c_dialect: FAIL (7 件)
MUTATION 2 RED: 本体から -Werror=implicit-function-declaration を外す (暗黙宣言が通る) -- check_c_dialect: FAIL (7 件)
MUTATION 3 RED: 本体から -Werror=implicit-int を外す -- check_c_dialect: FAIL (7 件)
MUTATION 4 RED: 偽の STATIC_ASSERT (_Static_assert(1, …) で条件を消す) -- check_c_dialect: FAIL (7 件)
MUTATION 5 RED: 本体を gnu89 に戻す -- check_c_dialect: FAIL (1 件)
MUTATION 6 RED: SQLite を gnu11 にする -- check_c_dialect: FAIL (1 件)
MUTATION 7 RED: SQLite の旗が本体の共通旗を継ぐ (§2 F2 の形に戻す) -- check_c_dialect: FAIL (1 件)
MUTATION 8 RED: os32_sqlite_test.o を本体の言語指定で組む -- check_c_dialect: FAIL (1 件)
MUTATION 9 RED: userland の SQLite 単体を gnu11 で組む -- check_c_dialect: FAIL (1 件)
MUTATION 10 RED: ブートの旗から言語指定を落とす (コンパイラの既定になる) -- check_c_dialect: FAIL (1 件)
MUTATION 11 RED: ブートの旗から拒否の旗を落とす -- check_c_dialect: FAIL (1 件)
MUTATION 12 RED: 公開 SDK ヘッダに行コメント -- check_c_dialect: FAIL (1 件)
MUTATION 13 RED: 公開 SDK ヘッダにブロック途中の宣言 -- check_c_dialect: FAIL (30 件)
MUTATION 14 RED: 公開 SDK ヘッダに stdbool -- check_c_dialect: FAIL (2 件)
MUTATION 15 RED: 公開 SDK ヘッダに指示付き初期化子 -- check_c_dialect: FAIL (30 件)
MUTATION 16 RED: SDK が配るライブラリヘッダに for の中の宣言 (gnu89 のアプリで通らない) -- check_c_dialect: FAIL (3 件)
MUTATION 17 RED: gnu89 の例 (sdk/example/hello) を gnu11 にする -- check_c_dialect: FAIL (1 件)
MUTATION 18 RED: 内部実装に _Atomic -- check_c_dialect: FAIL (1 件)
MUTATION 19 RED: 内部実装に restrict -- check_c_dialect: FAIL (1 件)
MUTATION 20 RED: 内部実装に #include /* C11 */ <stdatomic.h> と atomic_int (Codex P2-2 反例 a) -- check_c_dialect: FAIL (1 件)
MUTATION 21 RED: 内部実装に行継続で割った restrict (Codex P2-2 反例 b) -- check_c_dialect: FAIL (1 件)
MUTATION 22 RED: 内部実装に \ と改行の間に空白がある行継続で割った restrict (Codex 2 回目 反例 3) -- check_c_dialect: FAIL (1 件)
MUTATION 23 RED: 内部実装に #\vinclude <stdatomic.h> (Codex 2 回目 反例 4、VT) -- check_c_dialect: FAIL (1 件)
MUTATION 24 RED: 内部実装に #\finclude <stdatomic.h> (Codex 2 回目 反例 4、FF) -- check_c_dialect: FAIL (1 件)
MUTATION 25 RED: 公開 SDK ヘッダに展開されないマクロ #define OS32_ASSERT(x) _Static_assert(x, "x") (Codex 3 回目 (1)) -- check_c_dialect: FAIL (1 件)
MUTATION 26 RED: 保存済み lgy98.flags=5 のときだけ組まれる #if CONFIG_LGY98_FLAGS == 5 の中の _Atomic (Codex 3 回目 (5)) -- check_c_dialect: FAIL (1 件)
MUTATION 27 RED: 内部実装に複数行コメントの後ろの #include <stdatomic.h> (Codex 2 回目 P3) -- check_c_dialect: FAIL (1 件)
MUTATION CONTROL 28 GREEN: 対照: 内部実装に // とブロック途中の宣言
MUTATION CONTROL 29 GREEN: 対照: 公開 SDK ヘッダのコメントと文字列に // と restrict
MUTATION CONTROL 30 GREEN: 対照: lgy98.flags=0 なら #if CONFIG_LGY98_FLAGS == 5 の中は組まれない
MUTATION CONTROL 31 GREEN: 対照: 公開 SDK ヘッダのマクロ置換列の文字列の中の _Static_assert
MUTATION CONTROL 32 GREEN: 対照: 恒等 (何も変えない)
MUTATIONS 27/27 RED; CONTROLS 5/5 GREEN
```

## 旧版が見ていなかったもの (2026-10-01 の clang 版以前)

- 非定数式の `STATIC_ASSERT` (旧 `tss.c:17` の形) は gnu11 の `_Static_assert` が GCC の畳み込みで通すことがあり、探りでは拒否を要求しない
  (票 §5 の「`_Static_assert` の限界」— 実物は段 2 で `offsetof` にした)。
- 匿名構造体・共用体 (T0 で新規導入しない) は字句では見分けにくいので検査していない。
- 自作コードの旧式 (K&R) 関数定義の禁止は旗 (`-Werror=old-style-definition`) にしていない — `CFLAGS_COMMON` を継ぐ zlib (vendor) の 35 件が落ちるため。
- 内部実装の禁止語・禁止ヘッダは `make all` で組まれる翻訳単位を**実際の旗で**前処理した結果だけを見る。どの翻訳単位からも取り込まれない
  ヘッダ、`#if` で外れている部分 (別の設定・別の CPU 向け)、生成物のソースは数えない。
- 行継続で割った `#include` 指令の行番号は、GCC の行標識のとおり指令の終わりの物理行になる。
- **既知の限界 (Codex 3 回目、ユーザー決定で直さない)**:
  - (2) 翻訳単位の実際の引数に前処理の出力形式を変える旗 (`-P`・`-dM`・`-C` など) が混ざると、行標識が無くなって検出が空になる、
    またはコメントが残って誤検出になる。今の `build/*.mk` には無い。
  - (3) `#line` で木の外の論理ファイル名を付けたコードは、木の外として除外され見逃す。
  - (4) `_Pragma("region restrict")` のような pragma の引数は、前処理後に `#pragma region restrict` の行になり禁止語として誤検出する。
  - (P3) raw string (C では無効)、拡張識別子 (`restrict$x` の `$` を識別子の文字と見ない)、`strip_jobserver` が正規化されていない
    `MAKEFLAGS` (`-j 4` の分かれた形など) を扱わないこと — 親の make が正規化して渡すので通常の経路では起きない。
- ホスト gcc で組む NE2K のホスト試験 (`build/kernel.mk` の `gcc $(C_STD)`) は `i386-elf-gcc` の行でないので数えない。


## clang 版の回帰 (2026-10-01)

過去の反例は `test_c_dialect.py` で維持し、`test_clang_ast.py` で
`-P/-dM/-C/-CC`、外部名を付ける `#line`、pragma 内の restrict、`restrict$x`、
匿名メンバー・K&R 定義・typedef の禁止型を追加確認する。
行継続 include の報告は clang の物理開始行 (旧GCC行標識は終端行)。
内部 `__restrict` も型として restrict と判定する。
不正な C / ヘッダ不足は解析失敗として fail closed、変異の RED には数えない。
具体的な件数・コマンド・差分は [TASK_CLANG_CHECKS](../../docs/tasks/v3/TASK_CLANG_CHECKS.md) §6。
