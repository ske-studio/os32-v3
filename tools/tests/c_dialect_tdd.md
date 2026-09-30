# c_dialect (言語モードの検査、[C1]) — ホスト TDD の記録

票: [docs/tasks/v3/TASK_C11_MIGRATION.md](../../docs/tasks/v3/TASK_C11_MIGRATION.md) §6 段 4・段 5、受入 A1・A2・A5・A6
実装: `tools/check_c_dialect.py` (`make check-c-dialect`)
試験: `tools/tests/test_c_dialect.py` (`make check-c-dialect-host`、否定側は `--mutate`)

## 様式

検査器は旗の**文字列**でなく**コンパイル結果**で判定する。

1. `make -n -B all` のコンパイル行 (`i386-elf-gcc … -c`) を読み、旗の組 (−I・依存生成・`-c`・`-o`・ソースを除いたもの) ごとに
   `-E -dM` で `__STDC_VERSION__` / `__STRICT_ANSI__` を聞く。期待は SQLite 系 (`lib/sqlite3/{sqlite3,os32_sqlite_vfs,os32_sqlite_test}.c`、
   `userland/tests/sqlite_standalone/`) が gnu89、それ以外が gnu11。SQLite 系・`kernel/kernel.c`・`boot/boot_main.c` がコンパイル行に無ければ落ちる (空振りの番人)。
2. gnu11 の旗の組ごとに探りを 5 本コンパイルする: VLA・暗黙の関数宣言・暗黙 int・偽の `STATIC_ASSERT` は**期待する診断の文言つきで**拒否、
   真の `STATIC_ASSERT` は通る。`STATIC_ASSERT` は木の `include/types.h` の実物のマクロを使う。
3. 公開 SDK ヘッダ (`sdk/include/os32/*.h`。`os32_kapi_shared.h` もここ) は字句 (行コメント、`_Bool` `_Static_assert` `restrict` など C99/C11 の語、
   `<stdbool.h>` ほか) と、`-std=gnu89 -Wc90-c99-compat -Wc99-c11-compat -Wdeclaration-after-statement -Wlong-long -Werror` / `-std=gnu11 -Werror` の両方での取り込み。
   SDK が配るライブラリヘッダ (`build/sdk.mk` の `SDK_LIB_HEADER_DIRS`・`rt`・`lib/utf8.h`) は gnu89 と gnu11 で取り込めること
   (`rt/dbgserial.h` の可変引数マクロは C99 の機能だが gnu89 の GNU 拡張で通るので、ここは「取り込める」までを見る)。
   `sdk/example/hello` は Makefile の `-std` が gnu89 のままで、in-tree の SDK ヘッダで gnu89 のままコンパイルできること
   (apps/game は組まない — ユーザー決定。A5 の代わりの確認)。
4. 内部実装 (`kernel fs exec drivers gfx net lib include kapi arch platform boot userland sdk`、vendor
   `lib/sqlite3 lib/zlib lib/microtar lib/fatfs lib/os32_lz4 fs/fatfs userland/rust` を除く) の `_Atomic` `_Thread_local` `__thread` `restrict`
   `<threads.h>` `<stdatomic.h>` を字句で探す (コメント・文字列・`__restrict` は数えない)。

`make`・エミュレータ・配備は使わない (要るのはクロスコンパイラと `make -n` だけ)。
`make -n` には親の make のコマンドライン変数 (`C_STD=…` など) を渡し (-j / ジョブサーバは渡さない)、
`BUILD_OUT` を一時ディレクトリに向ける (`build/config.mk` の `$(shell mkdir -p …)` は `-n` でも走るため。
そのため `build/out/lgy98.flags` の試験カーネルの選択は見えず、LAN の `-D` は既定の組で読む — 言語モードには効かない)。実物の木で約 1.3 秒。

## 試験の区分 (65 チェック)

| 区分 | 何を固定したか |
|---|---|
| 1 字句 | **翻訳段階の順** (行継続の除去 → コメントを空白に → 判定): `#include /* … */ <x.h>`・複数行コメントを挟んだ `#include`・行継続で割った `restrict` / `#include` / `//` を元の行番号で見つける。`//` の行、文字列・ブロックコメント・文字定数の中の `//` を数えない、`\"` で文字列を抜けない、行継続の先の行番号、`restrict` と `__restrict` の区別、`#include` の `<…>` と `"…"` |
| 2 コンパイル行 | `i386-elf-gcc -c` の行だけ拾う (`-o` が先でも)、旗の組から −I・依存生成・`-c`・`-o` を外し `-include` は残す、期待する言語モード (SQLite 系 4 種は gnu89、本体・ブート・userland は gnu11) |
| 3 実際の言語モード | 親の `MAKEFLAGS` から変数指定だけを元の順で取り出す (-j・`--jobserver-auth` は捨てる)。`-std=gnu11`/`gnu89`/`c11`、`-std` 2 つは後ろが効く、`-std` 無しはコンパイラの既定 (gnu17) で gnu11 とは読まない |
| 4 拒否の探り | 旗が揃えば全部拒否、`-Werror=vla`・`-Werror=implicit-function-declaration`・`-Werror=implicit-int` をそれぞれ外すとその探りが通る、別の理由 (壊れた `-include`) の失敗を拒否と数えない、条件を捨てる `STATIC_ASSERT` を見逃さない、負サイズ配列の `STATIC_ASSERT` でも偽は拒否、マクロが無ければ真の探りが通らない |
| 5 公開 SDK ヘッダ | C89 の書き方だけのヘッダは通る (文字列の `//` 含む)。行コメント・ブロック途中の宣言・`for` の中の宣言・`_Bool`・`<stdbool.h>`・指示付き初期化子・`_Static_assert`・`restrict`・`long long` をそれぞれ拒否。ヘッダが 1 本も無ければ落ちる |
| 6 内部実装 | `#include /* C11 */ <stdatomic.h>` と行継続で割った `restrict` も拒否。C11 で許す `//`・ブロック途中の宣言・コメントと文字列の中の語・vendor は数えない。`_Atomic`・`_Thread_local`・`restrict`・`<threads.h>`・`<stdatomic.h>` を拒否 |
| 7 実物の木 | 実物の木で rc=0、要約に gnu11 と gnu89 の単位数。`make check-c-dialect C_STD=-std=gnu89` は落ち、`make -j4 check-c-dialect` は通る |

## RED → GREEN

### R0 — 検査器が無い (2026-09-30)

段 4 の試験を先に書き、`tools/check_c_dialect.py` が無い状態で回した:

```
$ python3 -B tools/tests/test_c_dialect.py
FileNotFoundError: [Errno 2] No such file or directory: '…/tools/check_c_dialect.py'
```

実装後: `53 checks, 0 failed` / `PASS`。

### 検査器の誤りを試験が捕まえるか (写しの木で検査器だけを差し替えて試験を回す)

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
MUTATION 12 RED: 公開 SDK ヘッダに行コメント -- check_c_dialect: FAIL (2 件)
MUTATION 13 RED: 公開 SDK ヘッダにブロック途中の宣言 -- check_c_dialect: FAIL (30 件)
MUTATION 14 RED: 公開 SDK ヘッダに stdbool -- check_c_dialect: FAIL (1 件)
MUTATION 15 RED: 公開 SDK ヘッダに指示付き初期化子 -- check_c_dialect: FAIL (30 件)
MUTATION 16 RED: SDK が配るライブラリヘッダに for の中の宣言 (gnu89 のアプリで通らない) -- check_c_dialect: FAIL (3 件)
MUTATION 17 RED: gnu89 の例 (sdk/example/hello) を gnu11 にする -- check_c_dialect: FAIL (1 件)
MUTATION 18 RED: 内部実装に _Atomic -- check_c_dialect: FAIL (1 件)
MUTATION 19 RED: 内部実装に restrict -- check_c_dialect: FAIL (1 件)
MUTATION 20 RED: 内部実装に #include /* C11 */ <stdatomic.h> と atomic_int (Codex P2-2 反例 a) -- check_c_dialect: FAIL (1 件)
MUTATION 21 RED: 内部実装に行継続で割った restrict (Codex P2-2 反例 b) -- check_c_dialect: FAIL (1 件)
MUTATION CONTROL 22 GREEN: 対照: 内部実装に // とブロック途中の宣言
MUTATION CONTROL 23 GREEN: 対照: 公開 SDK ヘッダのコメントと文字列に // と restrict
MUTATION CONTROL 24 GREEN: 対照: 恒等 (何も変えない)
MUTATIONS 21/21 RED; CONTROLS 3/3 GREEN
```

## 見ていないもの (既知の限界)

- 非定数式の `STATIC_ASSERT` (旧 `tss.c:17` の形) は gnu11 の `_Static_assert` が GCC の畳み込みで通すことがあり、探りでは拒否を要求しない
  (票 §5 の「`_Static_assert` の限界」— 実物は段 2 で `offsetof` にした)。
- 匿名構造体・共用体 (T0 で新規導入しない) は字句では見分けにくいので検査していない。
- 自作コードの旧式 (K&R) 関数定義の禁止は旗 (`-Werror=old-style-definition`) にしていない — `CFLAGS_COMMON` を継ぐ zlib (vendor) の 35 件が落ちるため。
- ホスト gcc で組む NE2K のホスト試験 (`build/kernel.mk` の `gcc $(C_STD)`) は `i386-elf-gcc` の行でないので数えない。
