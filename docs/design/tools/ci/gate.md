# tools/ci/gate.py — 手本の CI (CI_GATE_DESIGN)

> 状態: **現行 (2026-11-17)** — 出所: ユーザーの指示 (2026-11-17「既存の物の作りには欠点がありそうなので…そちらを改修するためのモデル的な ci を組んで」)。機種依存の分離の枝 (`wt/sep-*`) の workflow `sep` をこの形で組み、既存の workflow (check・build・full-check) を直す時の手本にする。既存の workflow はこの段では変えない。
ソース: `tools/ci/gate.py`、`tools/check_time.py` (`CHECK_LOG_DIR`)、`.github/actions/gate-setup/action.yml`、`.github/workflows/sep.yml`、試験 `tools/tests/test_ci_gate.py` (検査 `ci-gate-host`)。生成物の一致は [sep_identity.md](sep_identity.md)

## 1. 既存の CI の欠点 (2026-11-17 の実物と実行履歴)

| # | 欠点 | 根拠 |
|---|---|---|
| F1 | **変異込みの検査が関門になっていない。** 毎回の CI (check・build) は変異なしで、変異込みは夜間の `full-check` だけ。落ちても何も止まらないので、main に赤が溜まる | 夜間の full-check は直近 4 回のうち 3 回が失敗 (11-14・11-15・11-17)。同じ SHA の check・build は成功。11-17 (`75c7c6f69`) は `c-dialect`・`np21w-ini-live-host`・`x87-lifecycle-host` と ppc-compile のジョブが失敗。`x87-lifecycle-host` は `c94bdc0f4`、`ppc-hello-build` は `6dbae6efa` から変異の目印がずれたままだった |
| F2 | **失敗がログから読めない。** 変異の「期待どおりの失敗」の出力が全部 1 本のログに流れる | 11-17 の full-check の失敗ログは 1.4 GB・1,200 万行。そのうち 1,087 万行が同じ 1 行の繰り返し。落ちた検査の名前は `*** [...check-<名>] Error` を grep しないと分からない |
| F3 | **1 本の失敗が残りを隠す。** `tools/checks/run.py --run all` は最初の失敗で止まる。ジョブの `needs` で後ろのジョブも走らない | check.yml・full-check.yml の ppc-compile は `--run all`。11-17 は ppc-compile が 16 分で落ち、`ppc-qemu` は skipped |
| F4 | **検査の一覧が YAML に手書きで写っている** | check.yml の static-check は `make check-<名>` を約 30 行並べる (選び方は冒頭の註の手順で人が決めた)。`tools/ci/ppc_scope.py` は共通・PowerPC・x86 のパスの一覧を別に持つ |
| F5 | **用意の手順がジョブごとに書き写されている** | `apt-get install … gcc-13 qemu-user ccache …` の行が check.yml に 2 回、full-check.yml に 2 回、少しずつ違う中身で在る |
| F6 | **環境が足りない検査が、飛ばされるか、途中で落ちる** | クロスコンパイラの無いジョブでは PowerPC の検査が SKIP で通る (build.yml の check-fast)。手元では `gcc-13` が無いと `FileNotFoundError` で落ち、変更が原因か環境が原因かをログを読んで判断することになる |
| F7 | **CI と手元で入口が違う** | CI は YAML の中の shell、手元は `make check-<名>` か `run.py --run <名>`。`r1-fixture-host` は後者だと `KeyError: 'BUILD_OUT'` で落ちる (make の変数が要る) |
| F8 | **時間がかかり、どこが遅いかが成果物を開かないと分からない** | full-check は 1 ジョブで 147 分 (11-17) |
| F9 | **生成物が基点と同じかを確かめる仕組みが無い** | 移すだけの変更のたびに、手元で `tools/build_compare.py` と `.o` の比較を 20〜40 分かけて回した |

## 2. 手本の決まり

決まりの本文は [POLICY_CI.md](../../../POLICY_CI.md) §1 (G1〜G12) が正典。ここには、どの決まりが §1 のどの欠点を直すかだけを置く。

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

1. 道具の事前確認 (G6): `i386-elf-gcc` (`$CROSS_DIR/bin`)・`powerpc-elf-gcc` (`$PPC_CROSS_DIR/bin`)・`nasm`・`gcc-13`・`qemu-i386`・`qemu-system-ppc`・`genisoimage`・`e2fsck`・`cargo`。足りない物を入れ方と一緒に全部挙げ、終了コード 2。
2. このシャードの検査の名前を `tools/checks/run.py --list --shard` から取る (G4)。
3. `make -k -jN check CHECK_SHARD=… MUTATE=1` (`--no-mutate` なら `check-fast`、`MUTATE=0`) を、`CHECK_LOG_DIR=<置き場>/logs`・`CHECK_TIMES=<置き場>/check_times.tsv`・`OS32_REQUIRE_PPC=1` で回す。前提の準備・並列・順序は make の今の仕組みのまま (検査の走らせ方をもう 1 つ作らない)。
4. `check_times.tsv` (検査・秒・終了コード) と検査ごとのログから表を作る: 通過・失敗・回っていない・SKIP の行がある。
5. `<置き場>/results.json`、`<置き場>/failed-logs/` (落ちた検査のログの末尾 4 MiB)、画面と `$GITHUB_STEP_SUMMARY` に表。失敗か、回っていない検査が 1 本でもあれば終了コード 1。検査は全部通ったのに make が落ちた時 (前提の準備、木が変わっていないことの照合) も 1。

SKIP の行は挙げるだけで落とさない: 試験が自分の出力として `SKIP` と印字する物 (飛ばす時の振る舞いを確かめる試験) と見分けられないため。環境が揃っていれば本物の SKIP は `OS32_REQUIRE_PPC=1` で失敗になる。

### 3-2. 検査ごとのログ `tools/check_time.py`

`CHECK_LOG_DIR` があると、検査の出力を `<CHECK_LOG_DIR>/<検査>.log` に落とす (標準出力と標準エラーを 1 本に)。無ければ今までどおり画面に出す。`make check`・`check-fast`・`check-changed` のどれでも効く。

### 3-3. workflow `sep`

| ジョブ | 中身 |
|---|---|
| `checks` (シャード 0〜5、`fail-fast: false`) | `gate-setup` → `gate.py run --shard N/6` → 成果物 (`results.json`・`check_times.tsv`・`make.log`・`failed-logs/`) |
| `identity` | `gate-setup` → `tools/ci/sep_identity.py` (結果をジョブの要約へ) |

ジョブのあいだに `needs` は無い。検査の名前は YAML に 1 つも無い。

### 3-4. 用意 `gate-setup`

`build-setup` (x86 のクロスコンパイラ・Rust・フォント・apt) と `ppc-setup` (PowerPC のクロスコンパイラ) を呼び、足りない apt の物 (`gcc-13`・`qemu-system-ppc`) と `requirements.txt` を入れ、`$CROSS_DIR/bin` を PATH に足す。ジョブの側には apt の行を書かない。

## 4. 既存の workflow を直す時の当て方

直す時の手順と順序は [POLICY_CI.md](../../../POLICY_CI.md) §2 (R1〜R8) が正典。ここには、既存の 3 つの workflow のどこを何に替えるかの対応だけを置く。

| 既存 | 今の形 | 手本の形にするには |
|---|---|---|
| check.yml `static-check` | `make check-<名>` を手で並べる | クロスコンパイラ無しで回る検査に CHECK の `env` で印を付け、`gate.py run` に環境の絞り込みを足して一覧を登録から取る (G4)。道具の事前確認の表を環境ごとに持つ |
| check.yml `ppc-compile`・`ppc-qemu` | `run.py --run all`、`needs` の鎖、`ppc_scope.py` のパスの一覧 | `gate.py run` に替える (G2・G3)。PowerPC を回すかどうかの判定は、パスの一覧ではなく所有者集合 (`build/select.mk`) から引く |
| build.yml `check-fast` (6 シャード) | `make -k check-fast CHECK_SHARD=…` | `gate.py run --no-mutate --shard …` に替えると、表・検査ごとのログ・回っていない検査の検出が付く (G2・G3)。`gate-setup` にすると PowerPC の検査が SKIP にならない (G5・G6) |
| full-check.yml | 1 ジョブで `make check -k -j4 MUTATE=1`、ログ 1 本 | `sep` の `checks` と同じ行列にする (G1・G3・G8)。夜間ではなく push ごとの関門にするかは、実測の時間を見て決める |
| 用意 | apt の行が 4 か所 | `gate-setup` に寄せる (G5) |

## 5. 入れた時に確かめたこと・測ったこと

| 何を | 結果 |
|---|---|
| 手元 (WSL、`gcc-13` を入れた後) で `gate.py run --shard 0/6 --no-mutate` | 108 本を回して全部通過、回っていない検査 0。検査の時間の和は 901 秒 (うち `check-c-dialect` が 388 秒)。SKIP の行がある検査は `check-ppc-realhw-link` の 1 本 (`SKIP AF_PACKET: [Errno 1] fixture` — 試験が飛ばす時の振る舞いを確かめるために自分で印字する行で、本物の SKIP ではない。§3-1 のとおり挙げるだけで落とさない)。画面に出たのは表の 25 行だけ、検査ごとのログは 108 本で計 480 KB |
| 道具の無い環境 (`CROSS_DIR`・`PPC_CROSS_DIR` を渡さない) | 検査を始めずに、足りない 2 つを入れ方と一緒に挙げて終了コード 2 |

`sep` の GitHub 上の初回の実行の後に、シャードごとの時間・見つかった赤・1 本わざと落とした実行 (G12) の結果をここに足す。

## 6. 範囲の外

既存の workflow の書き換え、NP21/W・実機の受入、検査の中身の直し (目印のずれなどは見つけた枝で別のコミットにして直す)。
