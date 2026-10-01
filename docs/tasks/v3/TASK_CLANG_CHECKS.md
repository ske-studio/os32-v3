# TASK_CLANG_CHECKS — C ソースの静的検査を clang の構文木で作り直す

> 状態: **実装中 (2026-10-01)** — ユーザー指示 (2026-10-01「現在の検査器をバックアップして作り直し」)。clang 21.1.8 と python3-clang (libclang) はホストに導入済み (ユーザーが `apt install clang libclang-dev python3-clang`)。コーダー Codex `gpt-6.1-sol`、レビュー Codex `gpt-6-astra`。
>
> 発行: PM (Claude Code `claude-opus-5-5`、2026-10-01)。関係: [TASK_C11_MIGRATION.md](TASK_C11_MIGRATION.md) (`check-c-dialect`、Codex 3 往復)、[TASK_T1_LEDGER.md](TASK_T1_LEDGER.md) §4-6 (`check-p2v`、Codex 3 往復)、[POLICY_DEV.md](../../POLICY_DEV.md)。

## 1. なぜ

自前の正規表現・字句解析で C のソースを読む検査器は、Codex のレビューで**型表記・キャスト・括弧・行継続・コメントの変形を突かれるたびに穴が見つかる**ことを繰り返した (T0 の `check-c-dialect`、T1f の `check-p2v`。どちらも 3 往復でユーザー決定により「guard、わざと作った入力への耐性は目標にしない」として決着)。型の解決・マクロの展開・GCC 拡張の解釈を **clang 本体に任せる**と、この種の穴が構造的に消え、検査器の保守とレビューが判定の規則に集中できる (ユーザー: 「独自検査器より信頼性があるのでは」)。

## 2. 範囲

| 検査器 | 今の作り | clang での作り直し (案 — 設計はコーダーが §4 に書いて Codex が見る) |
|---|---|---|
| `tools/check_p2v.py` (`make check-p2v`、[C5]) | 正規表現で物理キャスト・物理引数 | 構文木で「物理番地のマクロ・物理の名前 (`*_phys` など) に由来する整数 → ポインタのキャスト」と「ポインタ → 整数を装置・PTE・CR3 へ渡す引数」を、型とマクロ展開の位置で判定。例外一覧 `tools/check_p2v_allow.txt` は引き継ぐ |
| `tools/check_c_dialect.py` (`make check-c-dialect`、[C1]) | 前処理後の字句で C11 の機能を探す (654 行) | 言語モードの判定 (`-E -dM` で実効の `__STDC_VERSION__`) は今のまま。禁止の機能 (`_Atomic`・TLS・`restrict`・VLA など) と公開 SDK ヘッダの C89 互換は、構文木または clang の診断 (`-Wpre-c11-compat`、`-Wc11-extensions`、`-Wvla` など) で |
| `tools/check_le_access.py` (`make check-le-access`) | 外部形式 (LE) の直アクセスを正規表現で | 構文木で、媒体のバイト列への多バイト型のキャスト・逆参照 |
| `tools/check_arch_asm.py` (`make check-arch-asm`) | C の中の `hlt` / `cli` などの直書きを正規表現で | 構文木の `GCCAsmStmt` / `asm` の文字列 |
| `tools/audit_cast_align.sh` (手動の監査、docs/08_build.md §8-4) | grep で非整列アクセスの候補 | `-Wcast-align=strict` 相当の診断か構文木 |
| `tools/create_fat12_d88.py` | ビルドから呼ばれていない (`mkfat12.py` と重複) | **撤去** |

**変えないもの**: `check_privileged.py` (逆アセンブルは objdump で既に本物の道具)、`check_constraints.py` (ID の整合だけ)、試験の枠組み・変異の仕組み (`mutpar.py`、`*_host.c`)、カーネルを組むコンパイラ (i386-elf-gcc のまま。clang は検査にだけ使う)。

## 3. 進め方

1. **バックアップ**: 今の 5 本を `tools/legacy_checks/` へ写す (`git mv` ではなく写しを残し、作り直した版と同じ入力で流して結果を比べられるようにする)。`make check` からは外す。比べ終えて新しい版が受け入れられたら、撤去するかはユーザーが決める。
2. **共通の土台** (`tools/clang_ast/` など): `make -n` から各翻訳単位の実際の旗を取り (`check-c-dialect` の既存の取り方を流用)、i386-elf-gcc 固有の旗を clang 向けに読み替え (`-target i386-unknown-none-elf`、`-ffreestanding`、インクルード、マクロ)、libclang (python3-clang) で構文木を得る。clang が解析に失敗した翻訳単位は**黙って飛ばさず失敗にする**。
3. 5 本を順に載せ替える。各検査について: 今の版が見つけている違反・例外を新しい版も同じく扱うこと (結果の比較)、Codex がこれまでの往復で出した反例 (T0 §6-4・§7、T1 §4-6-R、`tools/tests/c_dialect_tdd.md` ほか) をすべて試験に入れること、変異は実行時に落ちるもの。
4. **CI**: `.github/workflows/check.yml` と `build.yml` に clang と python3-clang の導入を足す。

## 4. 設計 (コーダーが実装の前に書く)

実装前設計 (2026-10-01): 共通部 `tools/clang_ast/` に既存の make dry-run・コンパイル行抽出を移す。`make -n -B all` は一時 BUILD_OUT、保存設定と MAKEFLAGS の変数を引き継ぎ、stdin は /dev/null。生成 build_id.c は実ビルドの生成済みソースへ対応付ける。libclang の詳細前処理記録を有効にし、展開マクロの範囲と物理ファイル位置、canonical type を使う。error/fatal 診断または入力不足は検査失敗。解析不能を SKIP にしない。

| GCC の旗 | clang 解析用 |
|---|---|
| i386-elf-gcc | libclang、`--target=i386-unknown-none-elf` |
| -std / -m32 / -march / -ffreestanding / -I / -D / -include | 保持。クロス newlib の include を追加 |
| -c / -o / -MMD / -MP / -MF / -MT / -MQ | 出力・依存生成だけ除く (既存抽出) |
| -fno-toplevel-reorder / -fno-reorder-functions / -fno-reorder-blocks / -fno-reorder-blocks-and-partition / -fno-tree-loop-distribute-patterns | コード生成の指定なので除く |
| -mpreferred-stack-boundary=2 / -mincoming-stack-boundary=2 | コード生成の指定なので除く |
| -Wno-stringop-truncation / -Wno-format-truncation / -Wno-array-bounds / -Wno-maybe-uninitialized / -Wno-clobbered | GCC 向け警告設定は AST 検査へ引き継がない |
| -Wc90-c99-compat / -Wc99-c11-compat | SDK は clang の C99/C11 extension 診断で代替 |
| -P / -dM / -dD / -C / -CC | AST 取得では前処理出力制御を除く。言語モード確認は GCC のまま |

GCC 拡張はまず実際の旗で全翻訳単位を解析し、受け付けない具体的な構文と対応を §6 に記録する。製品 ABI やビルドコンパイラは変更しない。必要な解析専用の互換定義は共通部に限定し、構文を黙って削除しない。

- P2V: canonical type の整数→ポインタ cast のオペランドを括弧・中間 cast 越しに辿り、物理名・番地定数・物理マクロを判定。P2V 系呼出しは変換済み。物理 sink の引数中のポインタ→整数 cast を検出し、整数を返す get_phys や V2P の引数を再解釈しない。マクロ展開記録で user/lease の V2P、関数内 CONST を検出。既存 file:function:reason と stale 検査を継承。
- C dialect: GCC の実効モード・拒否探りは継承。内部の atomic/TLS/restrict/VLA/匿名 record/旧式関数は AST の型・宣言または clang 診断で判定。include は前処理記録、未展開の公開マクロは禁止機能の libclang token を見る。公開 SDK は clang の gnu89/C99/C11 extension 診断と gnu11 解析で確認。vendor は既存範囲を規則判定から除外 (解析失敗は免除しない)。#line の論理名で逃げず物理位置を使う。
- LE: 既存9ファイルの pointer cast について pointee の要求 alignment が上がるものを AST で検出。多バイト scalar への媒体 byte pointer cast も検出し、コメントや typedef 表記で逃げない。
- arch asm: GCC_ASM_STMT の template の文字列をコンパイラの token から取得し、連結・escape を解釈して hlt/cli/sti を判定。arch/*/arch_*.h と ARCH-ASM-OK の既存例外を継承。
- cast alignment: 同じ AST alignment 判定を全実ビルド TU に適用する手動監査。候補は列挙、解析失敗は非0。kernel/user/all の入口を維持。

比較は保存版を同じ実木で走らせ、基点の違反・例外数と新規検出の理由を §6 に記す。反例は有効な宣言を持つ TU で解析成功を先に要求し、規則の runtime RED を確認する。

## 5. 受入

- 5 本それぞれ: 実物の木で今の版と同じ違反 0 / 例外の扱い (差が出たらその理由を列挙)、これまでの反例をすべて検出、変異が実行時に RED。
- `make check` と `make check-changed` の両方から到達する。clang の解析失敗は失敗になる。
- CI の check / build が通る。
- `make all` rc=0、`make check` rc=0。


## 6. 実装結果 (2026-10-01、Codex GPT-6)

基点 `c90eed8`、ブランチ `wt/clang`。状態行・ROLES は変更しない。
5本を `tools/legacy_checks/` へコピーし、比較用にルート解決だけ1階層補正した。
共通部は `clang_ast/build.py` (既存の dry-run/旗抽出、分離した -j 数値も処理)、
`clang_ast/__init__.py` (i386 libclang、実旗、newlib/resource、型・align・物理位置)、
`clang_ast/dialect.py` (型・宣言・include・診断)、`clang_ast/align.py` (手動監査)、
`clang_ast/asm.py` (clangが展開したasm文字列の取得)。
make のターゲット名・P2V の65件の例外一覧は維持。make の全検査経路は新版だけを呼ぶ。
`create_fat12_d88.py` は撤去 (ビルド呼び出し無し、現行文書は撤去説明へ更新)。FD生成は mkfat12.py。
CI2本に apt の clang/libclang-dev/python3-clang を追加し、静的CIにはlibnewlib-devも追加。setup-python の場合は
apt の dist-packages へ共通部が到達する。

### 実木での比較

同じ worktree・保存設定で保存版と新版を実行した。

| 検査 | 保存版 | clang版 | 差・扱い |
|---|---|---|---|
| P2V | 違反0、例外65 | 違反0、例外65 | 実際の型・中間cast・任意typedefを解決。物理引数は関数のparameter名と明示CR3/DMA位置で選ぶ。paging の仮想rangeを除外。memmap の MEM_SHM_BASE/MEM_SQLITE_STACK_BASE に展開する linker記号だけの算術は整数layout値として扱う。直接の未変換pointer castを免除しない |
| C dialect | 346コンパイル行、gnu11=340/gnu89=6、10旗組、公開6/配布24、内部332、違反0 | 同じ言語/旗/ヘッダ数、内部333、違反0 | 旧版は一時BUILD_OUT内の生成build_id.cを飛ばしていた。新版は実生成ソースを解析 (重複行は解析部でまとめる)。匿名の「型に名前がないがfield名はある」recordを匿名memberと誤認しない。既存HostDrv wire protocolの匿名2memberはfield形を固定して保持、他の新規匿名memberは拒否 |
| LE | 対象9、文字列/strict警告0 | 対象9、AST違反0 | 型の別名・修飾子・分割代入に依存しない。コンパイラ無しSKIPを廃止 |
| arch asm | 278 C/headerを字句走査、違反0 | 実ビルドTUとincludeのAST、違反0/解析失敗0 | 連結literal・macro template・文字列化/貼り合わせも検査。ARCH-ASM-OKは実際のcomment tokenのみ、arch実装macroの出自も許可する。未使用header/非選択#ifは対象外 |
| 手動align | 固定旗で300ファイル、成功139/失敗161、警告46 | 実旗で345TU、成功345/失敗0、候補126 (行位置125) | 旧版のuserland include不足、対象外のnet/boot/vendor等が差。候補を違反0と報告しない |

さらに同じ **345TU・実際のGCC旗** で `-Wcast-align=strict` と比較:
GCCは全解析成功、79行位置、新版は125行位置。GCCだけの位置は0。
clangだけの46位置は全てSQLite: 実旗に `-w` がありGCC診断は抑止されるが、
AST監査は型のalignment増加をそのまま数える。候補126と位置125の差は同一行の複数cast。
比較ログは `/tmp/clang-legacy-{p2v,c,le,arch,align}.log`、
`/tmp/clang-align.log`、`/tmp/clang-align-comparison.log`。

### 反例・変異

P2Vの既存11試験を有効な宣言付きTUにし、複数語・連続cast・括弧・i32・
get_phys/4096+V2P・pointerが演算の外に残る形・HostDrv integer memberを維持。
既知の限界だった `* const volatile`、`(uptr const)`、任意typedefを追加。
整数を返すget_phys経由のphysical castも検出する。実物の写しへの注入は **ビルド対象のsysclk.c** または既存bootinfo.cへ行い、
clang解析成功を先に要求する (未ビルドのp2v_mutant.cを置くだけにはしない)。
V2Pは実物のinline関数とfixtureのmacroの両方を検査。

C dialectの過去の往復は既存test_c_dialect.pyに維持。
共通回帰test_clang_ast.pyは前処理出力旗、#line外部名、pragma、$識別子、
匿名memberと名前付きpointer/array fieldの対照、K&R定義、typedef atomic/restrict、TLS、clangのGNU raw string、
GNU foldingの非定数static assert、関数戻り値/関数pointer typedefの禁止型、inline V2P、linker layout、align、連結/macro asm、
文字列化/貼り合わせasm、文字列で偽装した例外印、構文/言語旗の解析失敗を確認する。
asm文字列はLLVM 21のclang_Cursor_getGCCAssemblyTemplateから取得。LLVM 18では
clang_getCursorPrettyPrintedで関数ASTを印字し、clang lexerでtemplateだけを取得する
(周囲の宣言が無い印字バッファは字句専用、判定元の実TUは解析成功が必須)。
独自のmacro展開は削除。両経路のtemplate一致を回帰試験に入れた。
匿名memberもclang_Cursor_isAnonymousRecordDeclで判定し、型に名の無い
名前付きpointer/array fieldを誤認しない。LLVM 18のFile比較は物理file名で合わせる。
共通規則の変異4本はPython構文を確認してから試験を実行し、AssertionErrorのruntime REDを要求する。
P2V注入10本、既存C dialectの27否定/5対照も解析失敗をREDとして数えない。

### clang と GCC の非互換・解析失敗

実木345TUにclang非対応のGCC構文は無く、製品ソース/ABIの変更・構文削除は不要。
旗の除去表は共通部の DROP が全量。-P/-dM/-dD/-C/-CC はAST取得では不要な出力制御として除く。
clangのresource headerをnewlibより先のsystem includeにし、freestandingの
stdatomic.hはclang側を使う (newlib側を先に選ぶとint_least等で解析が崩れた)。
newlib threads.hはmachine/_threads.hが無く失敗する: 実木では取り込まれず、
その単独の既存負側試験はfail closed確認でありruntime REDの変異数には入れない。
//はclangのgnu89診断だけでは落ちないためlibclang COMMENT tokenで補う。
line continuationを含むcommentのspellingも正規化。includeの報告行はclangの物理開始行
(旧版GCC行標識は終端行)。未展開の公開macroはlibclangのtokenを検査する。

### 検証・未確認

最終版のコマンドとrc (makeは全てstdin=/dev/null):

| コマンド | rc / 結果 |
|---|---|
| CROSS_DIR=/home/hight/opt/cross make all | 0、/tmp/clang-all-complete.log |
| CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 make check-changed | 0、全108ターゲットを変異込みで実行 (内部でmake check)、/tmp/clang-check-changed-final4.log |
| CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 python3 tools/tests/test_clang_ast.py --mutate | 0、12試験・4/4 runtime RED・コンパイル失敗0、/tmp/clang-common-final6.log |
| LLVM 18のlibclang/Python bindingで同じ共通試験 | 0、12試験、/tmp/clang18-common-final.log |
| python3 tools/gen_tests_inventory.py --write | 0、360行 |
| make check-docs-links check-docs-orphans check-docs-status check-tests-inventory check-constraints check-map | 0、/tmp/clang-docs-complete.log |
| git diff --check | 0 |

P2Vは11試験・10/10 runtime RED・コンパイル失敗0。
C dialectは27/27 RED・対照5/5 GREEN。解析失敗をREDに数えない。
全検査の既存Pythonホスト試験で4件+5件のSKIPあり。新clang/P2V試験のSKIPは0。
旧版5本の比較は全rc=0。途中でfixtureの宣言不足・typedef衝突、macro template、
行コメント等の問題を検出し、修正後に再実行した。

環境補助: NP21W_DIRはworktree内のbuild/clang-local-imagesを指定。
Linux ELF32ホスト試験はこの実行環境の制約に合わせ、検証時だけ
PYTHONPATH=/tmp/clang-test-envのsitecustomizeでqemu-i386経由にする
(既存/tmp/t1ep3-test-env/sitecustomize.pyの写し。multiprocessingはforkを選ぶ)。
製品ソース・試験の期待値・クロスコンパイルの実物は変えない。

クロス無しの静的CIでarch asmも確認したところ、vendorのmicrotar/zlibが
stdio.h/stdlib.hを必要とし、clang resourceだけでは解析失敗 (rc=1)。
check.ymlにlibnewlib-devを追加し、共通旗はクロスnewlibが存在しなければ
/usr/include/newlibを選ぶ。解析を飛ばす・偽の宣言で置き換える対応はしない。
この経路でstdio.hが選ばれる回帰を共通試験に追加した。
CROSS_DIRを存在しない位置、PATHを/usr/bin:/binにし、apt downloadした
libnewlib-devを/tmpに展開してSYSTEM_NEWLIBだけをその展開先へ向ける検証は、
arch/LEともrc=0 (/tmp/clang-ci-arch-final.log、/tmp/clang-ci-le-final.log)。
ホストの導入物は変更しない。

LLVM 18 + LLVM 21 resourceの混在もstdint.hで解析失敗 (rc=1) と確認。
同版18のresourceとbindingへ揃えた実木arch検査はrc=0
(/tmp/clang18-arch-matched.log)。CIのaptは同版を導入する。
NP21/W・NHD・配備・ini・Windows・ゲスト試験・実機・GitHub Actionsの実行は未確認。
コミット・pushは行わない。独立レビューとCI受入はPMへ渡す。


### レビュー 1 回目の対応 (2026-10-01、Codex GPT-6)

基点 `36c4a1d`、独立レビュー Codex astra の P2 5 件への対応。状態行は維持。

| 指摘 | 修正 | 回帰・変異 |
|---|---|---|
| 1 物理引数の漏れ | 既存 paging/CR3/DMA API の物理入力位置を `PHYSICAL_ARGS` へ明記。表にない paging API だけ仮引数名の推測を使用 | 実際の `kernel/paging.h` の宣言をincludeし、第4引数 `(u32)p` を拒否。名前省略の宣言も拒否。位置表の該当エントリを空にする変異がruntime RED |
| 2 部分式による免除 | 括弧・暗黙変換を剥いだ最外式がP2V系の呼出し/展開の場合だけ免除。V2Pを逆方向の免除に使わない。別名macroで範囲が潰れる場合はclangのspelling位置でP2V定義由来を確認 | レビューの `(char *)(phys + V2P(p))`、P2V/P2V_IO部分式の算術、別名内の算術を拒否。最外P2Vと定数別名は対照GREEN。部分式まで免除する変異がruntime RED |
| 3 GCCとの分岐差 | 実クロスGCC 13.2.0の `-dM -E -x c -` を実旗で取得。include/利用者の-D/-Uは抽出時だけ外し、GCC定義の後に元の順序で適用。clangの `-undef` とclang専用feature照会macroの-Uでclang定義を除去 | `#if __GNUC__ >= 5` 内のrestrictを拒否、`__clang__`/`__llvm__`/`__has_feature` 分岐が消えることを確認。GCC定義取り込みを外す変異がruntime RED。クロス不在・限定モード未指定は明示エラー |
| 4 LEと整列の混同 | 対象9ファイルの多バイト整数pointerへのpointer castをLE直アクセスとして独立判定。整列増加は別規則、手動align監査は整列規則のみ。整数NULLの型付けは媒体アクセスに数えない | `u32 *disk` の `*(u32 *)&disk[1]` は整列候補0でもLE違反。typedef、macro、分割castも拒否。LE独立判定を無効にする変異がruntime RED |
| 5 宣言以外の禁止型 | 全cursorのcanonical typeを検査。libclangが落とすcastのrestrict/sizeof型は解析済みASTの宣言表示をclang lexerで補足。sizeofの非定数評価でVLA型も検出 | 通常/macroの `(int *restrict)p`、sizeofのrestrict/atomic/VLA、restrictの複合リテラルを解析成功後に拒否。表示による補足を無効にする変異がruntime RED |

`test_clang_ast.py --mutate` は17試験、9/9 runtime RED、解析・コンパイル失敗0。
追加5変異も写しの木の検査器を変え、Python構文確認後のAssertionErrorを要求する。
atomic複合リテラルという初期fixtureはclangで無効だったため、解析失敗をREDに
数えず、合法なrestrict pointerの複合リテラルへ変更した。

クロス無しCIは `OS32_CLANG_CROSS_FREE=1` をcheck.ymlに明記し、各検査のstderrへ
「LIMITED」「GCC predefined macros/branches are NOT verified」を出す。
このモードはclang既定とsystem newlibによる選択分岐の静的検査に限定し、
GCC分岐同等性を合格としない。GCC条件分岐試験はそのモードで明示SKIP。
build.ymlのクロスあり経路は実GCC定義で検査する。

clang固有分岐を持つヘッダはクロスnewlibの `sys/features.h`、`sys/cdefs.h`、
`ssp/string.h`。リポジトリ内の自作ヘッダには該当なし。
vendorの `lib/sqlite3/sqlite3.c` は `__clang__`/`__has_feature`/`__has_extension` 分岐を持つ。
これらもクロスありではGCC側を選ぶ。定義の一致はGCCとclangの構文・builtin意味論の
完全同等性を保証しない。非選択#if・未使用header、対象9ファイル外のLE形式は検査範囲外。
補足表示で見つけた型機能の診断行は包含宣言の開始行。

検証時の補助環境は前回と同じ: `NP21W_DIR` はworktree内の
`build/clang-local-images`、`PYTHONPATH=/tmp/clang-test-env` はELF32ホスト試験を
qemu-i386経由にするsitecustomize (製品/試験期待値は変更しない)。
最初の `make all` はrc=0だったが、既存FDレシピの `/tmp/np21w` へのcopyが
失敗の警告を出した。worktree内出力先を指定した再実行もrc=0。
初回check-changedではP2V別名4件の誤検出と、共通試験にLE検査器を追加したことによる
check-map入力漏れを検出。上記spelling位置の補正とmap追記で修正した。
NP21/W・NHD・配備・ini・ゲスト/実機試験・GitHub Actions実行は対象外。
コミット・pushは行わない。確認レビューはPMから前回レビュアーへ渡す。

追加の互換性対応: clang専用feature macroを-Uした際、clang resourceのstddef.hが
`__has_feature` を無条件に呼び、解析失敗することを全検査で検出した。
クロスありではGCCの `-print-file-name=include` の標準ヘッダを先に使う。
GCCのi386 stddef.hはmax_align_tに__float128を含むため、解析targetを
`i386-unknown-linux-gnu` にする (generic none-elfはclangで__float128を拒否する)。
`-undef` + GCC定義でLinux/clang条件分岐を排除し、`-nostdinc` と明示includeで
ホストlibcを排除する。製品のGCC target/旗/ABIは変えない。
stddef.h/stdatomic.hが解析でき、size_tが4バイトである回帰も追加。
この途中のcheck-changedの解析失敗もREDの数には含めない。
クロス無しを再現したLE/arch検査は両方rc=0、限定範囲の通知を確認
(`/tmp/clang-review-cross-free.log`)。

LLVM 18の同版binding/libLLVM/resourceでの共通回帰も17試験rc=0。
初回の補助起動はmutparのimport path/libLLVMの探索が不足して失敗し、
環境を揃えて再実行した。LLVM 18ではcursorのspelling位置も別名呼出しへ潰れるため、
先頭tokenの物理位置も使う補正を追加 (算術別名の拒否は維持)。
sizeof(pointer-to-VLA)/_Alignof(VLA)は式自体が定数になるので、
型オペランドの非定数な配列寸法もclangの評価で検出する。
macroのtoken貼り合わせで生成したrestrictもAST表示後のlexerで拒否する。

旧P2V試験の明示的pointer→integer→pointer往復は、今回の「括弧・暗黙変換だけ剥ぐ」
要件では変換済みの最外式にならないため、GREENからphysical-castの拒否期待へ移した。
実木の製品コード・65例外は変更しない。

Clang 21の追加照会macro `__has_embed`/`__has_constexpr_builtin` もGCC 13に無いことを
両コンパイラで確認して除去し、GCC分岐回帰に加えた。
`-dM` に載らない特殊照会builtinの全応答まで一致させる方式ではない
(例えばGNU Cでの `__has_cpp_attribute` の有無に差がある)。この限界は
定義済みmacroの一致と区別する。実木にこの照会による分岐は無い。

最終検査の途中でコーダーが追加編集したため、検査自体の全PASSの後に
check_tree_unchangedが4ファイルの変化を検出し、check-changedはrc=2になった。
試験による実木改変ではなく、実行中の追加修正/票追記が原因。
差分を戻さず、最終版を凍結してcheck-changedを再実行する。


凍結した最終版での完了結果 (makeは全てstdin=/dev/null):

| コマンド | rc / 結果 |
|---|---|
| `CROSS_DIR=/home/hight/opt/cross make all < /dev/null` | 0、`/tmp/clang-review-all-final.log` |
| `CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 make check-changed < /dev/null` | 0、fullの108ターゲット、実木変更ガードも成功、`/tmp/clang-review-check-changed-frozen.log` |
| `CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 python3 tools/tests/test_clang_ast.py --mutate` | 0、17試験・9/9 runtime RED・コンパイル失敗0、`/tmp/clang-review-common-final.log`。同内容を凍結したcheck-changed内でも確認 |
| `CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 python3 tools/tests/test_p2v.py --mutate` | 0、11試験・10/10 runtime RED・コンパイル失敗0、`/tmp/clang-review-p2v-tests.log`。凍結したcheck-changed内でも確認 |
| 同版LLVM 18のbinding/libLLVM/resourceで共通回帰 | 0、17試験、`/tmp/clang-review-llvm18-complete.log` |

上記makeの補助環境は `NP21W_DIR=/home/hight/os32-v3-wt-clang/build/clang-local-images`、
check-changedはさらに `PYTHONPATH=/tmp/clang-test-env` (上記qemu補助) を指定。
P2Vは実木の違反0・例外65を維持。LE対象9ファイルで違反0、arch asm違反0・解析失敗0。
C dialectは346コンパイル行 (gnu11=340、gnu89=6)、内部333TU、違反0。
既存C dialect変異27/27 RED・対照5/5 GREEN。既存ホスト試験は4件+5件SKIP、
今回の共通/P2V回帰はSKIP 0。手動alignの候補数は今回再集計していない。
最終版を凍結した再実行ではソース/票を編集せず、実木変更ガードもrc=0。
結果の追記はそのコマンド終了後に行った。


### レビュー 2 回目の対応 (2026-10-01、Codex GPT-6)

基点 `58d370b`、独立レビュー Codex astra の残り P2 1 件への対応。状態行は維持。

`FUNCTION_DECL` / `VAR_DECL` に限った表示による補足検査を撤去し、Clang C++ API の
`RecursiveASTVisitor` で翻訳単位全体の **全 TypeLoc** を辿る補助器へ置換した。
libclang が公開しない型式と、定数化された配列長の元の式も訪問する。
修飾付き TypeLoc は既定の visitor が VisitTypeLoc を呼ばずに剥ぐため、
TraverseTypeLoc で剥ぐ前の restrict / atomic を判定し、基底の再帰を継続する。
宣言・式の個別種別は列挙しない。既存 canonical type / VLA の検査も維持。

補助器は解析済み TU と同じソース・GCC 定義込みの旗を使用し、マクロを Clang で
展開する。診断位置は SourceManager の物理ファイル・物理行で取得し、#line の
論理位置を使わない。相対 include は検査ルートを基準にし、.. を正規化して
vendor の既存除外と整合させる。別ルートのヘッダを回帰に追加した。補助器のコンパイル失敗・再解析失敗は検査失敗として扱う。
補助器は /tmp のユーザー別キャッシュへソース内容・LLVM パスをキーに生成し、
一時出力の完成後に置換する。CI の両 workflow に llvm-dev を追加した。
製品 C / ABI / クロスコンパイラは変更しない。

回帰にはレビューのファイルスコープ3例を含む14文脈で restrict / atomic を拒否し、
通常の int pointer を対照 GREEN とした。静的アサート、列挙定数、フィールドの
ビット幅と配列長、typedef 配列長 / typeof、alignof、generic 選択、関数引数と
戻り値、関数ポインタ typedef も含む。マクロ・貼り合わせの否定例と、文字列・
コメントの対照例も確認した。既存 cast / sizeof / 複合リテラルの回帰を維持。
初期案の「全宣言を表示」でも Clang が配列長を数値に畳んで8否定例を見逃すことを
実行時に確認し、表示への依存をなくした。

`CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 python3 tools/tests/test_clang_ast.py --mutate`
は rc=0、19試験、11/11 runtime RED、コンパイル失敗0
(`/tmp/clang-review2-native-final.log`)。補足経路を呼ばない変異と、全 TypeLoc の再帰を
無効にする C++ 変異の両方で AssertionError を要求する。コンパイル・解析の
ERROR は RED と認めない。atomic 変異も新補助器の atomic 判定を無効にする。

`NP21W_DIR=/home/hight/os32-v3-wt-clang/build/clang-local-images`
を指定した `CROSS_DIR=/home/hight/opt/cross make all < /dev/null` は rc=0
(`/tmp/clang-review2-all.log`)。生成 FD のコピー先は worktree 内の隔離ディレクトリ。
最終 `check-changed` も前回と同じ PYTHONPATH=/tmp/clang-test-env の qemu 補助と
この隔離先を使用し、票を含め実木を凍結して実行する。実行結果は完了報告で示す。
NP21/W・NHD・配備・ini・Windows・ゲスト試験・実機には触れず、commit / push はしない。


初回 check-changed は rc=2: 既存 LAN bridge の SIGUSR1 競合変異が GREEN、
新補助器の未正規化 `kernel/../lib/sqlite3` パスによる vendor の誤検出が1件。
LAN はソース無変更の単独再実行で全5変異 RED / rc=0
(`/tmp/clang-review2-lan-retry.log`)。後者は上記のルート基準・.. 正規化で修正し、
相対パス解決を無効にする追加変異も runtime RED を要求する。
初回でも既存 C dialect 変異27/27 RED・対照5/5 GREEN、P2V違反0・例外65。


最終版の `CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 make check-changed < /dev/null`
は **rc=0**、full の108ターゲット、実木変更ガードも成功
(`/tmp/clang-review2-check-changed-final.log`)。
補助環境は上記 NP21W_DIR と PYTHONPATH。実木は C dialect 333TU で OK、
P2V 違反0・例外65、LE 9ファイルで違反0、arch asm 違反0・解析失敗0。
共通19試験・11/11 runtime RED・コンパイル失敗0、既存 C dialect 82チェック失敗0・
27/27 RED・対照5/5 GREEN。既存ホスト試験の4件+5件SKIPは維持。
全体検証中はソース/票を変更せず、この結果だけ終了後に追記した。


### レビュー 3 回目の対応 (2026-10-01、Codex GPT-6)

基点 `33f0214`、独立レビュー Codex astra の P2 / P3 への対応。状態行は維持。

配列引数の角括弧内の修飾は QualType ではなく ArrayType にあるため、全 TypeLoc の
再帰中に `getIndexTypeQualifiers().hasRestrict()` も検査する。[C1] で禁止されている
restrict だけを拾い、禁止されていない static / const / volatile は拾わない。
宣言・定義・関数 typedef・sizeof 内の関数 pointer 型の4文脈で、restrict と
他の修飾との組合せを否定例、通常の配列および static / const / volatile を対照とした。
macro の restrict も検査する。この index qualifier 判定だけを外す C++ 変異を追加し、
コンパイル・解析成功後の AssertionError による runtime RED を要求する。

INSTALL.md と docs/08_build.md のホスト依存へ Ubuntu の llvm-dev を追記し、
LLVM 開発ヘッダ・libLLVM・libclang-cpp と clang/libclang/resource header の版を
揃えることを明記した。導入確認を Python binding のロードだけでなく
`visitor_binary()` による C++ 補助器の組み立てまで広げた。両確認コマンドは rc=0。
build.py の末尾空行も削除した。

検証は `CROSS_DIR=/home/hight/opt/cross`、make は全て `< /dev/null`。
`make all` は rc=0 (`/tmp/clang-review3-all.log`)。
前回と同じ補助環境を使用: NP21W_DIR は worktree 内の build/clang-local-images、
check-changed の PYTHONPATH は /tmp/clang-test-env (ELF32 ホスト試験の qemu 補助)。
実木を凍結して check-changed を実行し、結果は完了後に追記する。
NP21/W・NHD・配備・ini・Windows・ゲスト/実機試験は対象外、commit / push はしない。


最終 `CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 make check-changed < /dev/null`
は **rc=0**、full の108ターゲットと実木変更ガードを完了
(`/tmp/clang-review3-check-changed.log`)。上記補助環境を使用し、実行中の編集なし。
実木は P2V 違反0・例外65、C dialect 内部333TUでOK、LE 9ファイルで違反0、
arch asm 違反0・解析失敗0。共通20試験・12/12 runtime RED・コンパイル失敗0、
既存 C dialect 27/27 RED・対照5/5 GREEN。既存ホスト試験の4件+5件SKIPは維持。
単独の `CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 python3 tools/tests/test_clang_ast.py --mutate`
も rc=0 (`/tmp/clang-review3-common.log`)。
未コミット修正を含む `git diff --check "$(git merge-base main HEAD)"` で
main...HEAD 相当の全差分を確認し、rc=0・空白の指摘なし。
commitしないため、HEADだけのmain...HEADには修正前の末尾空行が残る。
この結果追記は全体検査の終了後に行った。

2026-10-01 CI 修正: 共通変異12本の有効モードを明記し、GCC predefined macros 取り込みを外す1本だけを `test_review_gcc_branches` と共通の限定モード条件・理由で SKIP、残り11本は runtime RED 必須とした (クロスありは12本すべて必須)。

### 6-8. build CI run 36819190065 の P2V 偽陽性 (2026-10-01、Codex GPT-6)

run の全文ログを確認した。runner は Ubuntu 24.04、apt の libclang / Python binding は
18.1.3、GCC は13.2.0。`LIMITED` 行は `check-arch-asm` 内の共通単体試験
`test_cross_free_newlib_headers` が PATH 探索を mock し、CROSS_DIR を存在しない
一時パス、OS32_CLANG_CROSS_FREE を1にして出した通知だった。build.yml 自体に
限定モード指定はなく、この行は本検査が限定モードへ落ちた証拠ではない。
CI の実コンパイラは `/home/runner/opt/cross/bin/i386-elf-gcc`、newlib は
`/home/runner/opt/cross/i386-elf/include`。build_cross.sh も両ファイルの存在を検証する。
手元は同じ配置で prefix が `/home/hight/opt/cross`、libclang は21.1.8。

4件の原因は LLVM 18 の位置情報差。別ファイルの `P2V_CONST` / `P2V_IO_CONST`
を `LZ4_TEMP_BUF` / `TVRAM_CHAR` / `TVRAM_ATRP` 経由で展開したキャストは、
cursor の spelling location が呼出し位置へ縮退し、`cursor.get_tokens()` も空になる。
クロスあり・なし双方で再現するため、限定モード特有の選択分岐の問題ではない。
clang21 の限定モードでは4件とも出ず、clang18では同じ4件が出た。

§4の「変換済み式の出所で判定」に沿い (a) を選択した。
`clang_getToken` でキャスト開始位置の単一トークンを取り、その物理定義位置が
P2V系定義内か判定する。異なるファイルにまたがるトークン範囲を必要とせず、
式の一部だけがP2Vである不正な足し算は引き続き拒否する。例外一覧の追加なし。
別ヘッダを実ファイルとして読む GOOD / IO / BAD の回帰試験を追加し、
LLVM18で修正前AssertionError (runtime RED)、修正後GREENを確認した。
単一トークン取得を無効にする共通変異も追加した。

クロス探索は PATH の実行可能GCC、次に CROSS_DIR/bin (未指定時は従来の手元prefix)。
見つかったGCCの実パスからnewlibを求め、stdio.hが欠けたらsystem newlibへ
逃げずエラー。FULL通知にGCC/newlibパスを出し、模擬限定モード単体試験の通知は
捕捉して本検査の通知と混ぜない。build.ymlはOS32_CLANG_CROSS_FREE=0を明示し、
クロス実行ファイル/newlib存在を事前確認する。GCC分岐試験・変異のSKIP条件も
環境変数単独ではなく実際のクロス可否に揃えた (クロスがあれば限定指定でもFULL)。

CI互換確認にはaptのclang18 / libclang18 / Python binding / LLVM開発パッケージを
`/tmp/os32-clang18` に展開した。Ubuntu24.04全体・CIツールチェーン再構築は行わず、
手元の同系GCC13.2.0/newlibとclang18.1.8で再現する。全てのmakeのstdinは/dev/null。
NP21/W・NHD・配備・iniは触らず、commit / pushはしない。最終検証結果は下記。

最終検証結果 (実木を凍結して実行し、終了後にこの表を追記):

- 完全モード: `CROSS_DIR=/home/hight/opt/cross OS32_CLANG_CROSS_FREE=0`。
- 限定モード: `PATH=/usr/bin:/bin CROSS_DIR=/tmp/os32-missing-cross OS32_CLANG_CROSS_FREE=1`。
- 両モードで `OS32_MUT_JOBS=4`。clang18の補助環境だけは、上記PATHの先頭へ
  `/tmp/os32-clang18/root/usr/lib/llvm-18/bin` を追加し、展開したPython bindingと
  libclang/libLLVMをPYTHONPATH/LD_LIBRARY_PATHで指定した。

| コマンド (makeは `< /dev/null`) | clang21 完全 | clang21 限定 | clang18 完全 | clang18 限定 |
|---|---|---|---|---|
| `make check-p2v check-arch-asm check-le-access` | 0 | 0 | 0 (再実行) | 0 |
| `python3 tools/tests/test_clang_ast.py --mutate` | 0、22試験・13/13 RED | 0、22試験中1 SKIP・12/12 RED・変異1 SKIP | 0、22試験・13/13 RED | 0、22試験中1 SKIP・12/12 RED・変異1 SKIP |
| `python3 tools/tests/test_p2v.py --mutate` | 0、11試験・10/10 RED | 0、11試験・10/10 RED | 0、11試験・10/10 RED | 0、11試験・10/10 RED |

全ての実ソース検査はP2V違反0/例外65、arch asm違反0/解析失敗0、LE違反0/9ファイル。
変異は全てruntime RED、コンパイル失敗0。限定側のSKIPはGCC分岐同等性のみ。
ログは `/tmp/clang-ci-{full,limited,18-full,18-limited}-{0,1,2}.log`。
clang18完全モードの初回makeだけは **rc=2**: 展開したUbuntu26.04のLLVM18パッケージに
補助器が探す `libLLVM.so*` の名前がなく、type visitorが明示エラーになった。
`/tmp` 内に同じlibLLVMへのリンクを補い、再実行 **rc=0** を確認した
(`/tmp/clang-ci-18-full-make-retry.log`)。この環境不備を変異のREDには数えない。

`PYTHONPATH=/tmp/clang-test-env CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 make check-changed < /dev/null`
は **rc=0**、fullの108ターゲットと実木変更ガードを完了
(`/tmp/clang-ci-check-changed.log`)。既存のELF32ホスト試験用qemu補助を使用した。
C dialectは333TU、27/27 RED・対照5/5 GREEN。既存ホスト試験の4件+5件SKIPは維持。
クロスが存在する状態で `OS32_CLANG_CROSS_FREE=1` としてもFULL/GCC定義/クロスnewlib
になること、別prefix `/tmp/os32-ci-runner/opt/cross` とCI同様のPATH先頭指定でも
GCC/newlibを認識することもrc=0で確認した。GitHub上のCI再実行は未実施。
