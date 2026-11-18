# tools/ci/gate.py — 手本の CI (CI_GATE_DESIGN)

> 状態: **現行 (2026-11-17)** — 出所: ユーザーの指示 (2026-11-17「既存の物の作りには欠点がありそうなので…そちらを改修するためのモデル的な ci を組んで」)。機種依存の分離の枝 (`wt/sep-*`) の workflow `sep` をこの形で組み、既存の workflow (check・build・full-check) を直す時の手本にする。既存の workflow はこの段では変えない。
ソース: `tools/ci/gate.py`、`tools/check_time.py` (`CHECK_LOG_DIR`)、`.github/actions/gate-setup/action.yml`、`.github/workflows/sep.yml`、試験 `tools/tests/test_ci_gate.py` (検査 `ci-gate-host`)。生成物の一致は [sep_identity.md](sep_identity.md)

## 1. 既存の CI の欠点 (2026-11-17 の実物と実行履歴)

| # | 欠点 | 根拠 |
|---|---|---|
| F1 | **変異込みの検査が関門になっていなかった。** 毎回の CI は変異なしで、変異込みは夜間の `full-check` だけだった。2026-11-18 に、build の `checks` と check の PowerPC の段を変異込みにし、`full-check` を無くした | 夜間の full-check は 11-14〜17 の 4 回のうち 3 回が失敗し、同じ SHA の check・build は成功していた。変異の目印が古い検査が、数日〜数週間そのままだった |
| F2 | **失敗がログから読めない。** 変異の「期待どおりの失敗」の出力が全部 1 本のログに流れる | 11-17 の full-check の失敗ログは 1.4 GB・1,200 万行。そのうち 1,087 万行が同じ 1 行の繰り返し。落ちた検査の名前は `*** [...check-<名>] Error` を grep しないと分からない |
| F3 | **1 本の失敗が残りを隠す。** `tools/checks/run.py --run all` は最初の失敗で止まる。ジョブの `needs` で後ろのジョブも走らない | check.yml・full-check.yml の ppc-compile は `--run all`。11-17 は ppc-compile が 16 分で落ち、`ppc-qemu` は skipped |
| F4 | **検査の一覧が YAML に手書きで写っている** | check.yml の static-check は `make check-<名>` を約 30 行並べる (選び方は冒頭の註の手順で人が決めた)。`tools/ci/ppc_scope.py` は共通・PowerPC・x86 のパスの一覧を別に持つ |
| F5 | **用意の手順がジョブごとに書き写されている** | `apt-get install … gcc-13 qemu-user ccache …` の行が check.yml に 2 回、full-check.yml に 2 回、少しずつ違う中身で在る |
| F6 | **環境が足りない検査が、飛ばされるか、途中で落ちる** | クロスコンパイラの無いジョブでは PowerPC の検査が SKIP で通る (build.yml の check-fast)。手元では `gcc-13` が無いと `FileNotFoundError` で落ち、変更が原因か環境が原因かをログを読んで判断することになる |
| F7 | **CI と手元で入口が違う** | CI は YAML の中の shell、手元は `make check-<名>` か `run.py --run <名>`。`r1-fixture-host` は後者だと `KeyError: 'BUILD_OUT'` で落ちる (make の変数が要る) |
| F8 | **時間がかかり、どこが遅いかが成果物を開かないと分からない** | full-check は 1 ジョブで 147 分 (11-17) |
| F9 | **生成物が基点と同じかを確かめる仕組みが無い** | 移すだけの変更のたびに、手元で `tools/build_compare.py` と `.o` の比較を 20〜40 分かけて回した |

## 2. 手本の決まり

決まりの本文は `docs/POLICY_CI.md` §1 (G1〜G12) が正典。ここには、どの決まりが §1 のどの欠点を直すかだけを置く。

| 欠点 | 直す決まり | この workflow での形 |
|---|---|---|
| F1 関門になっていない | G1 | `checks` が push ごとに全検査を変異込みで回す |
| F2 失敗がログから読めない | G3 | 検査ごとのログ、ジョブの要約に表、成果物は落ちた検査のログだけ |
| F3 1 本の失敗が残りを隠す | G2 | `make -k`、`fail-fast: false`、ジョブのあいだに `needs` 無し、結果の行が無い検査は失敗 |
| F4 一覧が YAML に手書き | G4 | YAML に検査の名前が無い。`tools/checks/run.py --list --shard` から取る |
| F5 用意が書き写されている | G5 | `gate-setup` の 1 か所 |
| F6 道具が足りないと飛ばすか途中で落ちる | G6 | 事前確認で全部挙げて終了コード 2、`OS32_REQUIRE_PPC=1`、SKIP の行を表に |
| F7 入口が違う | G7 | `run:` は `gate.py run` の 1 行。手元でも同じコマンド |
| F8 時間がかかり、遅い所が見えない | G10 | 6 シャード、遅い 10 本を要約に |
| F9 生成物の一致を確かめる仕組みが無い | G8 | `identity` のジョブ ([sep_identity.md](sep_identity.md)) |

G9 (道具の試験) は `ci-gate-host`・`sep-identity-host`・`check-times-host`、G11 (同じ入力から同じ生成物) の例外は RAM root の ISO ([sep_identity.md](sep_identity.md) §2)、G12 (赤の実演) は §5。

## 3. 形

### 3-1. 入口 `tools/ci/gate.py run`

    python3 -B tools/ci/gate.py run --shard <番号>/<総数> --out <置き場> [--no-mutate] [--jobs N]
    python3 -B tools/ci/gate.py run --env qemu-ppc --shard <番号>/<総数> --out <置き場>

1. 道具の事前確認 (G6): `i386-elf-gcc` (`$CROSS_DIR/bin`)・`powerpc-elf-gcc` (`$PPC_CROSS_DIR/bin`)・`nasm`・`gcc-13`・`qemu-i386`・`qemu-system-ppc`・`genisoimage`・`e2fsck`・`cargo`。足りない物を入れ方と一緒に全部挙げ、終了コード 2。
2. このシャードの検査の名前を `tools/checks/run.py --list --shard` から取る (G4)。
3. `make -k -jN check CHECK_SHARD=… MUTATE=1` (`--no-mutate` なら `check-fast`、`MUTATE=0`) を、`CHECK_LOG_DIR=<置き場>/logs`・`CHECK_TIMES=<置き場>/check_times.tsv`・`OS32_REQUIRE_PPC=1` で回す。前提の準備・並列・順序は make の今の仕組みのまま (検査の走らせ方をもう 1 つ作らない)。
4. `check_times.tsv` (検査・秒・終了コード) と検査ごとのログから表を作る: 通過・失敗・回っていない・SKIP の行がある。
5. `<置き場>/results.json`、`<置き場>/failed-logs/` (落ちた検査のログの末尾 4 MiB)、画面と `$GITHUB_STEP_SUMMARY` に表。失敗か、回っていない検査が 1 本でもあれば終了コード 1。検査は全部通ったのに make が落ちた時 (前提の準備、木が変わっていないことの照合) も 1。

`--env <環境>` を付けた時は、その環境を要る検査 (`run.py --list --env`) だけをシャードに割り、手順 3 の make の代わりに、検査を 1 本ずつ順に `tools/checks/run.py --run <検査> --env <環境>` で回す (`run_each`)。1 本落ちても残りを回し、所要は同じ `check_times.tsv` に足す。QEMU で機械を丸ごと模擬する検査は、並べて回すと期限に間に合わなくなるので、シャードの中は直列にする。check.yml の `ppc-qemu` がこの入口を使う。

SKIP の行は挙げるだけで落とさない: 試験が自分の出力として `SKIP` と印字する物 (飛ばす時の振る舞いを確かめる試験) と見分けられないため。環境が揃っていれば本物の SKIP は `OS32_REQUIRE_PPC=1` で失敗になる。

### 3-2. 検査ごとのログ `tools/check_time.py`

`CHECK_LOG_DIR` があると、検査の出力を `<CHECK_LOG_DIR>/<検査>.log` に落とす (標準出力と標準エラーを 1 本に)。無ければ今までどおり画面に出す。`make check`・`check-fast`・`check-changed` のどれでも効く。

### 3-3. workflow `sep`

| ジョブ | 中身 |
|---|---|
| `checks` (シャード 0〜5、`fail-fast: false`、**手動で起動した時だけ**) | `gate-setup` → `gate.py run --shard N/6` → 成果物 (`results.json`・`check_times.tsv`・`make.log`・`failed-logs/`) |
| `identity` | `gate-setup` → `tools/ci/sep_identity.py` (結果をジョブの要約へ) |

ジョブのあいだに `needs` は無い。検査の名前は YAML に 1 つも無い。
分離の作業のあいだは、push で回すのは `identity` だけにしている (ユーザーの指示 2026-11-17「今回の実装に必要な分だけ」)。`checks` は Actions の Run workflow か `gh workflow run sep.yml --ref <枝>` で起動する。POLICY_CI の G1 (変異込みを push ごとに) は既存の workflow を直す時の目標で、この workflow はまだ満たしていない。

### 3-4. 用意 `gate-setup`

`build-setup` (x86 のクロスコンパイラ・Rust・フォント・apt) と `ppc-setup` (PowerPC のクロスコンパイラ) を呼び、足りない apt の物 (`gcc-13`・`qemu-system-ppc`) と `requirements.txt` を入れ、`$CROSS_DIR/bin` を PATH に足す。ジョブの側には apt の行を書かない。

## 4. 既存の workflow を直す時の当て方

直す時の手順と順序は `docs/POLICY_CI.md` §2 (R1〜R8) が正典。ここには、既存の 3 つの workflow のどこを何に替えるかの対応だけを置く。

| 既存 | 今の形 | 手本の形にするには |
|---|---|---|
| check.yml `static-check` | `make check-<名>` を手で並べる | クロスコンパイラ無しで回る検査に CHECK の `env` で印を付け、`gate.py run` に環境の絞り込みを足して一覧を登録から取る (G4)。道具の事前確認の表を環境ごとに持つ |
| check.yml `ppc-compile` | `run.py --run all`、`ppc_scope.py` のパスの一覧 | `gate.py run --env ppc-cross` に替える (G2・G3)。PowerPC を回すかどうかの判定は、パスの一覧ではなく所有者集合 (`build/select.mk`) から引く |
| check.yml `ppc-qemu` | 2026-11-18 に `gate.py run --env qemu-ppc --shard i/4` の 4 分割にし、`needs` の鎖を外した (G2・G3・G10)。検査ごとの所要を成果物 `check-times-<SHA>-ppc-qemu-<i>` に出す | 残りは `ppc_scope.py` のパスの一覧と、用意の行の重複 |
| build.yml `checks` (6 シャード) | `make -k check CHECK_SHARD=…` (全部の検査を変異込み、2026-11-18 から) | `gate.py run --shard …` に替えると、表・検査ごとのログ・回っていない検査の検出が付く (G2・G3)。`gate-setup` にすると PowerPC の検査が SKIP にならない (G5・G6) |
| 用意 | apt の行が 4 か所 | `gate-setup` に寄せる (G5) |

## 5. 入れた時に確かめたこと・測ったこと

| 何を | 結果 |
|---|---|
| 手元 (WSL、`gcc-13` を入れた後) で `gate.py run --shard 0/6 --no-mutate` | 108 本を回して全部通過、回っていない検査 0。検査の時間の和は 901 秒 (うち `check-c-dialect` が 388 秒)。SKIP の行がある検査は `check-ppc-realhw-link` の 1 本 (`SKIP AF_PACKET: [Errno 1] fixture` — 試験が飛ばす時の振る舞いを確かめるために自分で印字する行で、本物の SKIP ではない。§3-1 のとおり挙げるだけで落とさない)。画面に出たのは表の 25 行だけ、検査ごとのログは 108 本で計 480 KB |
| 道具の無い環境 (`CROSS_DIR`・`PPC_CROSS_DIR` を渡さない) | 検査を始めずに、足りない 2 つを入れ方と一緒に挙げて終了コード 2 |

| GitHub 上の初回 (`80183dbc0`、6 シャード・変異込み) | 計 701 本を回し、回っていない検査 0、失敗 15。シャードの所要は 14〜33 分 (検査の時間の和は 2,340〜5,576 秒)。`identity` は用意を含めて約 2 分半で、x86 は一致、PowerPC は 949 本の `.o` が一致 |
| 失敗 15 本の内訳 | QEMU の検査 11 本 (用意に VGA の ROM が無く `QMP EOF` — `gate-setup` と事前確認を直した)、`check-docs-tree` (この段の文書の相互リンク)、`check-check-times-host` (この段の試験が `CHECK_LOG_DIR` を継いだ)、`check-target-toolchain-host` (既存の試験の期待値に手元のパスが直書き)、`check-np21w-ini-live-host` (main の元からの赤、変異の目印のずれ)。5 つとも直した |

直した後の全シャードの緑と、1 本わざと落とした実行 (G12) は**未確認** (手動で `checks` を起動して確かめ、ここに足す)。

## 5-1. 所要の棚卸し (2026-11-18)

道具は [time_inventory](time_inventory.md)、手順はスキル `.claude/skills/os32-ci-inventory`。

| 何を | 前 | 後 |
|---|---|---|
| check の `ppc-qemu` (QEMU の検査 12 本、変異込み) | 1 本のジョブで直列、53〜61 分 (`6cad7d729`・`6453f88da`・`a685b788b`)。内訳は `ppc-link-sh-run` 約 17 分・`ppc-fb` 約 11 分・`arch-ctx-guest` 約 10 分・`ppc-usb` 約 9 分・`ppc-fp-guest` 約 6 分・残り 7 本で約 6 分 (ログの時刻から。誤差は数十秒) | 4 本に割って並べる。最初の回 (`8078bf349`、新しい枝) は check の全体が 46 分 (前は ppc-compile と鎖で 64〜80 分)、シャードは 46・31・18・40 分。うち 13〜15 分ずつは PowerPC のクロスコンパイラのビルドだった (下の 1)。検査だけの所要は 25.4・9.7・4.2・21.3 分。この回の記録で QEMU の検査 12 本の重みを入れた。**直した後の回 (`9b9bbba9b`、クロスコンパイラのキャッシュあり): check の全体は 22 分** (`ppc-qemu` の 4 本は 16・20・22・14 分、検査だけなら 15.0・13.8・13.6・12.7 分で偏り 1.17。`ppc-compile` 7 分、`static-check` 5 分半) |
| build の `checks` 6 シャード (x86 の検査 706 本、変異込み) | 検査の秒の和は 40.8〜78.5 分、最大 ÷ 最小 1.92。ジョブの実時間は 14〜29 分 (`a685b788b`)、ほかの回は 10〜39 分。重みは変異なしの計測のままで、実測と 2 倍以上ずれた検査が 92 本 | 重みを変異込みの 5 回の中央値で作り直して回した (`8078bf349`) が、**悪くなったので戻した**: 検査の秒の和は 28.9〜93.9 分 (最大 ÷ 最小 3.25)、実時間は 10〜31 分。x86 の重みは元のまま (下の 2・3)。直した後の回 (`9b9bbba9b`) の実時間は 12〜30 分 (23・15・17・15・30・12 分)、検査の秒の和は 39.0〜107.5 分で偏り 2.76 — 揃っていない |

待ち時間の全体 (push から最後のジョブが終わるまで): 前は 64〜80 分 (check の PowerPC の 2 段が決めていた) → 後は 30〜36 分 (build の一番遅いシャードが決める。`9b9bbba9b` は 30 分、`cc5ce5948` は check 17 分・build 36 分)。main の最初の回 (`9b9bbba9b`) で `ppc-toolchain-cache` がクロスコンパイラを組み (13 分)、main のキャッシュに保存した。次に縮めるなら x86 のシャードの偏り (下の 2・3、票 TASK_CI_REWORK の段 S6・S7)。

この棚卸しで分かった事:

1. **キャッシュは枝ごと。** 枝が読めるキャッシュは自分の枝と main の物だけで、main の push は PowerPC の検査を範囲の判定で回さない事があるので、PowerPC のクロスコンパイラのキャッシュは main に無かった。新しい枝の最初の回は、PowerPC の各ジョブが 13〜15 分かけて組んでいた (`ppc-compile` が 4 分の回と 19〜21 分の回があったのはこのため)。check.yml に、main でキャッシュだけを保つジョブ `ppc-toolchain-cache` を足した。
2. **検査の秒は、並んで回るほかの検査に左右される。** build のシャードの中は検査を並列に回すので、記録の秒は CPU を取り合った分を含む。同じ検査が回ごとに ±5 割動き、割り振りを変えると値も変わる。秒の中央値で重みを作り直しても揃わなかった。CPU 時間で測る形にするのが筋で、票 TASK_CI_REWORK の段に入れた。
3. **QEMU の検査が x86 のシャードで「重い検査」として数えられている。** 12 本は x86 のシャードでは 1〜2 秒で終わる (ゲストを起動しない) のに、割り振りでは予算の値 (合計 13,500 秒、x86 の検査の実測の合計 22,346 秒の 6 割) で数えられ、それが入ったシャードは実際には軽かった。実測の重み (合計 3,638 秒) を入れて偏りの元は 4 分の 1 になった。x86 のシャードから外す (SKIP で通さない) のは票の段 S6。
4. **変異の試験が落とすコアファイルが、別の検査を落とす。** qemu-user の中の試験をわざと落とす変異が `qemu_fixture_<時刻>_<pid>.core` を作業木の根に残し、同じシャードで同時に走った `check-map` が「宣言の無いファイル」として落ちた (`8078bf349` の checks (3))。割り振りが変わって重なっただけで、前から在った競合。`tools/checks/run.py --run` が検査を始める前にコアの上限を 0 にする形に直した。

## 6. 範囲の外

既存の workflow の書き換え、NP21/W・実機の受入、検査の中身の直し (目印のずれなどは見つけた枝で別のコミットにして直す)。
