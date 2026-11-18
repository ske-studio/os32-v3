# tools/check_c_dialect.py — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_C11_MIGRATION (§6 段 4・段 5、受入 A1・A2・A5・A6)・TASK_CLANG_CHECKS の契約を今のソースと照らして要約
ソース: `tools/check_c_dialect.py` (`expected_std`・`effective_std`・`probe_rejects`・`check_build_flags`・`check_sdk_headers`・`check_sdk_lib_headers`・`check_sdk_sample`・`check_internal_units`)・`tools/clang_ast/build.py` (`dry_run`・`strip_jobserver`・`parse_compile_lines`)・`tools/clang_ast/dialect.py`

規則 [C1] (`docs/CONSTRAINTS.md`: 内部は GNU11、公開 SDK ヘッダは C89/GNU89、SQLite は GNU89、暗黙の宣言・int・VLA 等の禁止) を、旗の **文字列ではなくコンパイル結果** で確かめる (`make check-c-dialect`)。
位置づけは「誤って持ち込むことを防ぐ guard」で、わざと作った入力への耐性は目標にしない。

## 1. コンパイル行と言語モード

- `make -n -B all` の出力から `i386-elf-gcc … -c` の行を拾い、旗の組 (`-I`・依存生成・`-c`・`-o`・ソースを除く。`-include` は残す) ごとにコンパイラへ `-E -dM` で `__STDC_VERSION__` / `__STRICT_ANSI__` を聞いて実際のモードを決める (`effective_std`)。`-std` が 2 つあれば後ろが効き、無ければコンパイラの既定 (gnu17) で、gnu11 とは読まない。
- 期待 (`expected_std`): `lib/sqlite3/sqlite3.c`・`lib/sqlite3/os32_sqlite_vfs.c` (ビルドが `BUILD_OUT` に作る写しも同じ扱い)・`userland/tests/sqlite_standalone/` が **gnu89**、それ以外 (本体・ブート・userland・`lib/sqlite3/os32_sqlite_test.c`) が **gnu11**。
- 空振りの番人: `REQUIRED_SRCS` (SQLite の 2 本と `os32_sqlite_test.c`、`sqlite_standalone` の 2 本、`platform/pc98/kernel.c`、`boot/boot_main.c`) がコンパイル行に無ければ落ちる。コンパイル行が 1 本も無くても落ちる。
- `make -n` への引き継ぎ (`dry_run`): 親の `MAKEFLAGS` から並列の引き継ぎ (`-j…`・`--jobs`・`--jobserver-auth=…`・`--jobserver-fds=…`) だけを除き、残り (`e` などの単文字旗、` -- ` 以降の変数指定) はバイト列のまま子に渡す (エスケープの解釈は子の make に任せる)。`BUILD_OUT` はコマンドラインで一時ディレクトリに向ける (`-n` でも走る `$(shell mkdir …)` が実物の木に書かないように)。旗を変える保存設定 (`SAVED_SETTINGS` = `lgy98.flags`) は実物の `build/out` から一時の `BUILD_OUT` に写してから `make -n` する。

## 2. 拒否の探り

gnu11 の旗の組ごとに小さな本文をコンパイルし、**期待する診断の文言つきで** 拒否されることを見る: VLA (`-Werror=vla`)・暗黙の関数宣言・暗黙の int・偽の `STATIC_ASSERT` (`include/types.h` の実物のマクロ)。真の `STATIC_ASSERT` は通ること。別の理由 (壊れた `-include` など) の失敗は拒否と数えない。

## 3. 公開 SDK ヘッダと配るもの

- 公開 SDK ヘッダ (`sdk/include/os32/*.h`、`os32_kapi_shared.h` を含む) は gnu89 (`-Wc90-c99-compat -Wc99-c11-compat -Wdeclaration-after-statement -Wlong-long -Werror`) と gnu11 (`-Werror`) の両方で取り込めること。加えて clang の構文木と字句で、ヘッダ自身の行に C99/C11 の語 (`SDK_FORBIDDEN`: `_Bool`・`_Static_assert`・`restrict`・`<stdbool.h>` など) と行コメント `//` (行継続で割ったものも) が無いこと。この木で展開されないマクロの置換列も見る (外部アプリが展開すれば届くため)。ヘッダが 1 本も無ければ落ちる。
- SDK が配るライブラリヘッダ (`build/sdk.mk` の `SDK_LIB_HEADER_DIRS` ほか) は gnu89 と gnu11 で取り込めること。
- `sdk/example/hello` は gnu89 のまま in-tree の SDK ヘッダでコンパイルできること (外部の apps / game は組まない。その代わりの確認)。

## 4. 内部実装

1 の翻訳単位のうち vendor (`lib/sqlite3`・`lib/zlib`・`lib/microtar`・`lib/fatfs`・`lib/os32_lz4`・`fs/fat/fatfs`・`userland/rust`) を除いたものを、**その単位の実際の引数で** clang に解析させ (`#if`・マクロ・行継続・`#include` の解釈は本物に任せる)、木の中のファイルに次が出たら落とす: `_Atomic`・`_Thread_local`・`__thread`・`restrict` (`__restrict` も型として)・`<stdatomic.h>`・`<threads.h>`・VLA・匿名の構造体 / 共用体・旧式 (K&R) の関数定義。システムヘッダと vendor のファイルは数えない。報告の行は物理行。
**解析に失敗したら違反として落とす** (fail closed)。`#if` で外れた部分と、どの単位からも取り込まれないヘッダは見ない。

検査: `tools/tests/test_c_dialect.py` (否定側は `--mutate`) と `tools/tests/test_clang_ast.py`。過去の反例 (コメント入りの `#include`、行継続で割った語、VT / FF、`make -e`・変数指定のエスケープ、保存済みの `lgy98.flags` の `#if` の中、SDK の展開されないマクロ) を試験に持つ。
