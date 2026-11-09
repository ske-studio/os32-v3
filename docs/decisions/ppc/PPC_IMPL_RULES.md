# PPC_IMPL_RULES — PowerPC の段の実装と取り込みの決まり (CI を落とした型と試験の形)

> 状態: **現行 (2026-11-10)** — PowerPC の段 (分離の段・到達点 ② の段) を実装者に頼むときと、PM が取り込む前に手元で洗う型。各段の依頼から参照する。段の状態は票 TASK_PPC_SEP・TASK_PPC_KBOOT が持つ。

> 出所: ~/os32-tmp/run/ppc-common/CI_LESSONS.md (2026-11-09〜10、PowerPC の PM が段ごとに足した 16 項目) を整理して写した。以後はこの文書を正とする。

CI は統合の枝で os32-v3 の PM が 1 回だけ回すので、PowerPC の枝では**手元の検査が CI の代わり**。前の段で CI を落とした型を全部洗ってから push する。

## 1. CI を落とした型 (push の前に洗う)

| # | 型 | 洗い方 |
|---|---|---|
| 1 | 関数名・ファイル名で書いた例外の一覧 (tools/check_p2v_allow.txt・check_privileged_allow.txt・check_kprintf_window_allow.txt)。関数を動かす・名前を変えると古くなる | `make check-p2v check-privileged check-arch-asm check-le-access` を 1 本ずつ |
| 2 | 生成物の鮮度 (check-vk32-crc-host)。生成器や生成物の一覧 (build/kernel.mk の KAPI_GENERATED など) を触った | clean ビルドの後に `make check-vk32-crc-host` |
| 3 | ソースの一覧の 3 か所 (build/sources/order.mk・選択の一覧・tools/tests/fixtures/build_profile_x86.json の C_KERNEL、PORT_FLOW の l.85) | 3 か所を揃え、`python3 -B tools/tests/test_build_selection.py` |
| 4 | 写しの木で組む試験 (subprocess で gcc、HEADERS/TREE_FILES/MIRROR/FILES の一覧、関数の切り出し、自前の -I、間接 include、.c の include、`__ASSEMBLER__` で cpu.h を読む物) | 触ったヘッダ・ソースの名前を tools/・build/・Makefile・docs/ で grep し、全部追従 |
| 5 | tools/checks/*.py と試験の CHECK の covers/inputs が、どのファイルにも当たらない glob になる (check-map が落ちる) | 新しいファイルを covers に足す。covers の衝突は ast で 3 方向の和にして検算する (正規表現で切ると `[ch]` の glob で壊れる) |
| 6 | tools/source_roots.py の SCAN_ROOTS (新しい置き場が走査の外) | 新しい置き場を足す |
| 7 | kdebug の import (kernel/module/kdebug_imports.def の source 欄、slot は詰めない・並べ替えない) と tools/tests/fixtures/dep_dir_a/l01.json の指紋。FEP の MODULE_ABI_GEN の指紋 (部品の contracts の字面から計算) | 指紋を変えない。contracts のヘッダはコメント 1 字も触らない |
| 8 | tools/tests/test_ci_stab.py の規則 (`if not mutant:` を書かない、covers に足す、試験の補助関数に `run` という名前を使わない) | `python3 -B tools/tests/test_ci_stab.py` |
| 9 | 新しい host32 の試験が 32 ビットの libc (crt1.o) に頼る (CI と WSL に無い、この PC にはあるので手元では通る) | `gcc -m32 -ffreestanding -fno-pie -no-pie -nostdlib -static`、`_start` から exit (手本 tools/tests/test_abi_boundary.py:41-51、tools/tests/host32.py) |
| 10 | PPC の selected の計測 (tools/ppc/known_header_dependencies.json・selected_success.json) | `python3 -B tools/ppc/check_compile.py --source-profile selected --accept --json` で減らさない |
| 11 | GeForce まわりを組む host 試験 | tools/tests/recover_gf_dev_stub.c のリンクが要る。main を取り込んだら GeForce の試験 (test_geforce・test_gf_*・recovery など) を回し直す |
| 12 | 新しいヘッダを既存のヘッダから include した (第 5 段 A で CI の build が落ちた) | `grep -rl '<既存のヘッダ>' tools/tests/test_*.py` の全部を実際に回す |
| 13 | 文書の枝 (文書だけの push では build の CI が走らず、統合の枝で初めて出る) | `make check-docs-tree` を**単独で**回して出力の NG を見る。票 (docs/tasks/) どうしは「上位:」と後続以外リンクせず、「票 TASK_<名>」と名前を文字で書く |

最後に `make check-map`・`make check-check-select-host`・`python3 -B tools/checks/run.py --lint`・`git diff --check`。前提のビルドが要る試験 (needs=['make:sqlite-module-proto'] など) は先にその make を回す。

## 2. 環境

- 対話シェルの CROSS_DIR は PowerPC (/home/hight/opt/cross-ppc)。**x86 の make と試験には必ず `CROSS_DIR=/home/hight/opt/cross` を明示する**。32 ビットの実行は `HOST32_RUNNERS=qemu`。
- 実装者の sandbox はネットに出られない。作業木を作ったら `~/os32-tmp/bin/seed_fonts.sh <作業木>` でフォントを写す (無いと make all がフォントの取得で落ちる)。
- make は 1 本ずつ (並行だとリンクが競合する)。全体の検査 (make check・check-fast・check-changed) は回さない。1 つの検査は 30 分まで。合格に数えるのは最後の変更より後のログだけ。
- 機械は 4 コア。実装者は同時に 3 本まで (4 本以上では check-p2v などが 30 分を超えた)。
- 既知の環境の失敗 (退行と数えない、報告に分けて書く): test_exec_hwb_abort はこの PC で落ちる (CI では通る)。test_gf5_fancheck の変異 fancheck-write と test_surface_lease の変異「check PT alignment」の生き残りは基点からの既存の不備。
- PowerPC の事実の正典は docs/ppc/。BSD の手本は ~/os32-tmp/ref/{netbsd,openbsd,freebsd}、メーカーの資料は ~/os32-tmp/ref/vendor/ (SOURCES.md)。**GPL のソース (QEMU・OpenBIOS・Linux) は読まない** (QEMU の文書は可)。

## 3. 試験の形

- 32 ビットで組む host 試験は**製品のコードをそのまま組み込み**、試験の側で置き換えるのは特権の操作と最後の遷移の観測点 (iret・ASM の入口・I/O・MMIO の bus など、ホストで実行できない物) だけ。製品の関数本体を試験に複製しない。変異は製品の行を変える。
- **変異の判定 (到達点 ② の段で 5 回指摘された)**: 変異ごとに「期待の停止 PC (専用の停止ラベル)・終了コード・診断の行」と「そこまでに出ているはずの到達済みの印」を表に持ち、**両方が一致したときだけ RED**。timeout・別の停止点・印の欠落・想定外の終了コード・QEMU の起動不能・compile の失敗は試験そのものの失敗 (rc≠0) で、SURVIVED とも RED とも区別する (確定設計 PPC_KBOOT_DESIGN §2)。
- QEMU の試験は 1 本 30〜60 秒、OF の起動は既存の ISO と QMP の stdio の方式 (tools/ppc/run.py・mmu_run.py・kboot_entry.py) を使い回す。

## 4. 取り込みと報告

- push は tip のコミットに `[skip ci]`。cherry-pick した tip にも付け直す。push の後に `git ls-remote origin refs/heads/<枝>` で SHA を確かめてから本線の PM に送る (推測の SHA を送らない)。`gh run list --branch <枝>` で CI が走っていないことも見る。
- 積んだ枝は main を取り込み直してから送る。取り込み後の検査は 1 本ずつ回し直す。
- **比較の主張には比べた 2 つの SHA を書く**。merge の前の build_compare の結果を merge の後の枝の説明に使わない。報告が参照する証拠のファイルが run の置き場にあることを確かめ、受入をする側に渡す。
- 段の独立レビューは書き手と別のモデル (ROLES §0)。P1・P2 の直しの確認は同じレビュアーに差分だけ。P3 だけや 30 行程度の機械的な直しは PM が読んで着地してよい。
- **受入は一括** (ユーザー指示 2026-11-10、os32-v3 の PM 経由:「受け入れですが、一つ一つのSHAの実装を確かめずに一度ブランチを切ってすべてマージして一括で確認してみては？そこで失敗したら逐次に戻してみて。でないと受け入れだけで時間を消費してしまいます。」): まだ受け入れていない枝は**前の段の上に積み、最後の 1 本だけで全部が入る形**にして送る。本線の PM はそれを一括受入の枝に足して台本の和を 1 回で回し、失敗したら逐次・二分で切り分ける。
- **受入の台本は最小** (ユーザー指示 2026-11-10、os32-v3 の PM 経由:「エミュレータでの確認がGUIまで波及しているのでテスト内容のミニマルなものを用意して実行したほうが良いのでは？」): x86 の段の NP21/W の台本は「本線の最小の受入 (自動、1 像 10〜15 分目安: HDD 起動・ver/mem・CUI の固定の組・スタックの最大・GUI の 1 往復・STOP) + その段が触った領域の追加の項目だけ」(例: 表示の段なら 200 行と撮影 2〜3 点、IME の段なら CUI の変換 1 回)。大きな項目は一括受入で落ちたときだけ広げる。
- **ソースを移す段は旧パスに空の規則を置く** (os32-v3 の PM 決定 2026-11-10、本線も同じ扱い): 差分ビルドの作業木では古い `.d` が旧パスを指したまま残り、`No rule to make target '<旧パス>'` で落ちる (第 5 段 C2 の kernel/ime/ime_render_tvram.c で起きた)。移した段で、その .o を組む mk に `<旧パス>: ;` を理由 1 行 (日付・移した先・差分ビルドの古い .d の対策) の注記つきで置く。空の規則は次の大きな整理で消してよい。
