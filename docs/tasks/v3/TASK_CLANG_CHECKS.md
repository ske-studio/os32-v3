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
