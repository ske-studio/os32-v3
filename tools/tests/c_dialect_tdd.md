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
3. 公開 SDK ヘッダ (`sdk/include/os32/*.h`、在れば `include/os32_kapi_shared.h`) は字句 (行コメント、`_Bool` `_Static_assert` `restrict` など C99/C11 の語、
   `<stdbool.h>` ほか) と、`-std=gnu89 -Wc90-c99-compat -Wc99-c11-compat -Wdeclaration-after-statement -Wlong-long -Werror` / `-std=gnu11 -Werror` の両方での取り込み。
   SDK が配るライブラリヘッダ (`build/sdk.mk` の `SDK_LIB_HEADER_DIRS`・`rt`・`lib/utf8.h`) は gnu89 と gnu11 で取り込めること
   (`rt/dbgserial.h` の可変引数マクロは C99 の機能だが gnu89 の GNU 拡張で通るので、ここは「取り込める」までを見る)。
   `sdk/example/hello` は Makefile の `-std` が gnu89 のままで、in-tree の SDK ヘッダで gnu89 のままコンパイルできること
   (apps/game は組まない — ユーザー決定。A5 の代わりの確認)。
4. 内部実装 (`kernel fs exec drivers gfx net lib include kapi arch platform boot userland sdk`、vendor
   `lib/sqlite3 lib/zlib lib/microtar lib/fatfs lib/os32_lz4 fs/fatfs userland/rust` を除く) の `_Atomic` `_Thread_local` `__thread` `restrict`
   `<threads.h>` `<stdatomic.h>` を字句で探す (コメント・文字列・`__restrict` は数えない)。

`make`・エミュレータ・配備は使わない (要るのはクロスコンパイラと `make -n` だけ)。実物の木で約 1.3 秒。

## 試験の区分 (53 チェック)

| 区分 | 何を固定したか |
|---|---|
| 1 字句 | `//` の行、文字列・ブロックコメント・文字定数の中の `//` を数えない、`\"` で文字列を抜けない、行継続の先の行番号、`restrict` と `__restrict` の区別、`#include` の `<…>` と `"…"` |
| 2 コンパイル行 | `i386-elf-gcc -c` の行だけ拾う (`-o` が先でも)、旗の組から −I・依存生成・`-c`・`-o` を外し `-include` は残す、期待する言語モード (SQLite 系 4 種は gnu89、本体・ブート・userland は gnu11) |
| 3 実際の言語モード | `-std=gnu11`/`gnu89`/`c11`、`-std` 2 つは後ろが効く、`-std` 無しはコンパイラの既定 (gnu17) で gnu11 とは読まない |
| 4 拒否の探り | 旗が揃えば全部拒否、`-Werror=vla`・`-Werror=implicit-function-declaration`・`-Werror=implicit-int` をそれぞれ外すとその探りが通る、別の理由 (壊れた `-include`) の失敗を拒否と数えない、条件を捨てる `STATIC_ASSERT` を見逃さない、負サイズ配列の `STATIC_ASSERT` でも偽は拒否、マクロが無ければ真の探りが通らない |
| 5 公開 SDK ヘッダ | C89 の書き方だけのヘッダは通る (文字列の `//` 含む)。行コメント・ブロック途中の宣言・`for` の中の宣言・`_Bool`・`<stdbool.h>`・指示付き初期化子・`_Static_assert`・`restrict`・`long long` をそれぞれ拒否。ヘッダが 1 本も無ければ落ちる |
| 6 内部実装 | C11 で許す `//`・ブロック途中の宣言・コメントと文字列の中の語・vendor は数えない。`_Atomic`・`_Thread_local`・`restrict`・`<threads.h>`・`<stdatomic.h>` を拒否 |
| 7 実物の木 | 実物の木で rc=0、要約に gnu11 と gnu89 の単位数 |

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

## 見ていないもの (既知の限界)

- 非定数式の `STATIC_ASSERT` (旧 `tss.c:17` の形) は gnu11 の `_Static_assert` が GCC の畳み込みで通すことがあり、探りでは拒否を要求しない
  (票 §5 の「`_Static_assert` の限界」— 実物は段 2 で `offsetof` にした)。
- 匿名構造体・共用体 (T0 で新規導入しない) は字句では見分けにくいので検査していない。
- 自作コードの旧式 (K&R) 関数定義の禁止は旗 (`-Werror=old-style-definition`) にしていない — `CFLAGS_COMMON` を継ぐ zlib (vendor) の 35 件が落ちるため。
- ホスト gcc で組む NE2K のホスト試験 (`build/kernel.mk` の `gcc $(C_STD)`) は `i386-elf-gcc` の行でないので数えない。
