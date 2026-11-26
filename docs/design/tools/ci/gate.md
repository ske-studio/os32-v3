# 旧 CI の設計と計測記録 (CI_GATE_DESIGN)

> 状態: **現行 (2026-11-17)** — 出所: ユーザーの指示 (2026-11-17「既存の物の作りには欠点がありそうなので…そちらを改修するためのモデル的な ci を組んで」)。機種依存の分離の枝 (`wt/sep-*`) の workflow `sep` をこの形で組み、既存の workflow (check・build・full-check) を直す時の手本にする。既存の workflow はこの段では変えない。
旧実行器は P7-2 で撤去した。実行時の仕様は `git show dafe81642:docs/design/tools/ci/gate.md`、現行の関門は [CI 設計](ci_v2.md)、生成物の一致は [sep_identity.md](sep_identity.md) を参照する。

## 1. 既存の CI の欠点 (2026-11-17 の実物と実行履歴)

| # | 欠点 | 根拠 |
|---|---|---|
| F1 | **変異込みの検査が関門になっていなかった。** 毎回の CI は変異なしで、変異込みは夜間の `full-check` だけだった。2026-11-18 に、build の `checks` と check の PowerPC の段を変異込みにし、`full-check` を無くした | 夜間の full-check は 11-14〜17 の 4 回のうち 3 回が失敗し、同じ SHA の check・build は成功していた。変異の目印が古い検査が、数日〜数週間そのままだった |
| F2 | **失敗がログから読めない。** 変異の「期待どおりの失敗」の出力が全部 1 本のログに流れる | 11-17 の full-check の失敗ログは 1.4 GB・1,200 万行。そのうち 1,087 万行が同じ 1 行の繰り返し。落ちた検査の名前は `*** [...check-<名>] Error` を grep しないと分からない |
| F3 | **1 本の失敗が残りを隠す。** 旧手順（4d70e2c233f0 の記録） は最初の失敗で止まる。ジョブの `needs` で後ろのジョブも走らない | 当時の通常・全件検査 の ppc-compile は `--run all`。11-17 は ppc-compile が 16 分で落ち、`ppc-qemu` は skipped |
| F4 | **検査の一覧が YAML に手書きで写っている** | 当時の検査 workflow の static-check は `make check-<名>` を約 30 行並べる (選び方は冒頭の註の手順で人が決めた)。`ppc_scope.py (旧版 d4d22d1c0028)` は共通・PowerPC・x86 のパスの一覧を別に持つ |
| F5 | **用意の手順がジョブごとに書き写されている** | `apt-get install … gcc-13 qemu-user ccache …` の行が 当時の通常・全件検査に各 2 回、少しずつ違う中身で在る |
| F6 | **環境が足りない検査が、飛ばされるか、途中で落ちる** | クロスコンパイラの無いジョブでは PowerPC の検査が SKIP で通る (当時のビルド workflow の check-fast)。手元では `gcc-13` が無いと `FileNotFoundError` で落ち、変更が原因か環境が原因かをログを読んで判断することになる |
| F7 | **CI と手元で入口が違う** | CI は YAML の中の shell、手元は `make check-<名>` か `run.py --run <名>`。`r1-fixture-host` は後者だと `KeyError: 'BUILD_OUT'` で落ちる (make の変数が要る) |
| F8 | **時間がかかり、どこが遅いかが成果物を開かないと分からない** | full-check は 1 ジョブで 147 分 (11-17) |
| F9 | **生成物が基点と同じかを確かめる仕組みが無い** | 移すだけの変更のたびに、手元で `tools/build_compare.py` と `.o` の比較を 20〜40 分かけて回した |

## 2. 手本の決まり

決まりの本文は `docs/POLICY_CI.md` §1 (G1〜G13) が正典。ここには、どの決まりが §1 のどの欠点を直すかだけを置く。

| 欠点 | 直す決まり | この workflow での形 |
|---|---|---|
| F1 関門になっていない | G1 | `checks` が全検査を変異込みで回す (決まりは main に入れる SHA の関門。push ごとに回るのは今の workflow の作りで、ユーザー決定 2026-11-25 で決まりから外した) |
| F2 失敗がログから読めない | G3 | 検査ごとのログ、ジョブの要約に表、成果物は落ちた検査のログだけ |
| F3 1 本の失敗が残りを隠す | G2 | `make -k`、`fail-fast: false`、ジョブのあいだに `needs` 無し、結果の行が無い検査は失敗 |
| F4 一覧が YAML に手書き | G4 | YAML に検査の名前が無い。旧手順（4d70e2c233f0 の記録） から取る |
| F5 用意が書き写されている | G5 | `gate-setup` の 1 か所 |
| F6 道具が足りないと飛ばすか途中で落ちる | G6 | 事前確認で全部挙げて終了コード 2、`OS32_REQUIRE_PPC=1`、SKIP の行を表に |
| F7 入口が違う | G7 | `run:` は `gate.py run` の 1 行。手元でも同じコマンド |
| F8 時間がかかり、遅い所が見えない | G10 | 6 シャード、遅い 10 本を要約に |
| F9 生成物の一致を確かめる仕組みが無い | G8 | `identity` のジョブ ([sep_identity.md](sep_identity.md)) |

G9 (道具の試験) は `ci-gate-host`・`sep-identity-host`・`check-times-host`、G11 (同じ入力から同じ生成物) の例外は RAM root の ISO ([sep_identity.md](sep_identity.md) §2)、G12 (赤の実演) は §5。

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

1. **キャッシュは枝ごと。** 枝が読めるキャッシュは自分の枝と main の物だけで、main の push は PowerPC の検査を範囲の判定で回さない事があるので、PowerPC のクロスコンパイラのキャッシュは main に無かった。新しい枝の最初の回は、PowerPC の各ジョブが 13〜15 分かけて組んでいた (`ppc-compile` が 4 分の回と 19〜21 分の回があったのはこのため)。当時の検査 workflow に、main でキャッシュだけを保つジョブ `ppc-toolchain-cache` を足した。
2. **検査の秒は、並んで回るほかの検査に左右される。** build のシャードの中は検査を並列に回すので、記録の秒は CPU を取り合った分を含む。同じ検査が回ごとに ±5 割動き、割り振りを変えると値も変わる。秒の中央値で重みを作り直しても揃わなかった。CPU 時間で測る形にするのが筋で、票 TASK_CI_REWORK の段に入れた。
3. **QEMU の検査が x86 のシャードで「重い検査」として数えられている。** 12 本は x86 のシャードでは 1〜2 秒で終わる (ゲストを起動しない) のに、割り振りでは予算の値 (合計 13,500 秒、x86 の検査の実測の合計 22,346 秒の 6 割) で数えられ、それが入ったシャードは実際には軽かった。実測の重み (合計 3,638 秒) を入れて偏りの元は 4 分の 1 になった。x86 のシャードから外す (SKIP で通さない) のは票の段 S6。
4. **変異の試験が落とすコアファイルが、別の検査を落とす。** qemu-user の中の試験をわざと落とす変異が `qemu_fixture_<時刻>_<pid>.core` を作業木の根に残し、同じシャードで同時に走った `check-map` が「宣言の無いファイル」として落ちた (`8078bf349` の checks (3))。割り振りが変わって重なっただけで、前から在った競合。旧手順（4d70e2c233f0 の記録） が検査を始める前にコアの上限を 0 にする形に直した。

### 作り直し前との比較に使う記録

2026-11-25 の直近 20 push（確定した 18 件のうち成功 15 件）の最長所要は 2,999 秒。統合枝 `3b1cecee3` の全件 CI は check 45568507923・build 45568508091。取得できた 30 ジョブの本文は合計 139,581,115 バイト。RED の結果形式に合う 9,042 行には個別の結果とその総括の重複があり、SKIP の 123 行も大半を検査名に帰属できなかった。**移行前の変異総数は不明**。現行の比較には登録ごとの宣言数・実行数・検出数と通常・変異の所要を使う。

P3 の生成物比較の基点はタグ `v3-pre-restructure` のコミット `c23d187bc0621f51cea1883df032ef058526cf88`。P3 の受入記録では check 45661182928 と build 45661182759 はともに成功。保存した `SHA256SUMS` の SHA-256 は `8b5842ab79085c63ab70c3178823dffd7febd2e258ce04e6bd73a73679bf5639`。

## 5-2. 揺れの調べ (2026-11-18、票 TASK_CI_REWORK の段 S0)

| 揺れ | 調べた物 | 原因 | 直し |
|---|---|---|---|
| 用意の `rustup toolchain install` が `rust-src` の `detected conflict` で落ちる (run 44234810239 の 1 回目、`ppc-qemu (0)`) | 直近の check・build・sep の 49 ジョブのログ | 落ちた 1 本だけが `using existing install for nightly-2026-04-29` で始まった (ほかの 48 本は部品 4 つを新しく取る)。この手順より前に rustc を呼ぶ手順は無く、runner が rust-src のファイルは在るが部品として登録されていない toolchain を持っていた。手元で同じ状態 (`multirust-config.toml` と `components` から rust-src を外し、ファイルは残す) を作ると、同じ conflict で落ちる | `.github/actions/build-setup`: toolchain が在って rust-src が登録されていなければ、消してから入れる。判定はリポジトリの外で `RUSTUP_AUTO_INSTALL=0` を付けて打つ — 中で打つと `rust-toolchain.toml` を読んだ rustup が判定の前に toolchain を入れてしまう (最初の直し `af58ea349` の 10 回では 62 ジョブ全部がそうなり、判定が runner の元の状態を見ていなかった) |
| `ppc-sd-link` の QMP EOF (run 44016465654、11-17、枝 wt/sep-ci の build の checks 2/6) | その回のログ、同じ回の `ppc-mmu` | 揺れではない。用意に `qemu-system` (mac99 の VGA の ROM = seabios) が無く、QEMU が起動直後に終わった。同じ回の PowerPC の QEMU の検査 11 本が同じ形で落ちている。QEMU の stderr は成果物に残っていなかった | `50da7775f` (gate-setup に qemu-system、`gate.py` の道具の事前確認に firmware と VGA の ROM) で直っている。PowerPC の台本に潜む競合 2 つ (`sd_run.py` の UDP の口を閉じてから QEMU に bind させる間、`mmu_run.py`・`run.py` の `build/out/ppc` の固定の名前) は、証拠は無く PowerPC の PM が平時に直す |

10 回の確かめ (`af58ea349`、当時の検査 workflow を `arch=both` で手動起動): 10 回とも success。`ppc-sd-link`・`ppc-usb` は各回の `ppc-qemu` で回って通った (各 150〜182 秒・約 430 秒)。用意の失敗は 0。

判定の直しの後の 10 回 (`1ee8d5090`、同じく `arch=both` で 10 回): 10 回とも success。rustup の用意は 62 ジョブで、auto-install 0・部品を新しく取った 62・壊れた toolchain を見つけて消した 0・conflict 0 (この 10 回では runner の壊れた状態は出なかった)。`ppc-sd-link`・`ppc-usb` は各回で回って通った (11 回ずつ、push の回を含む)。票の基準 (5) を満たした。

## 5-3. 実測の取り直し (2026-11-18、票 TASK_CI_REWORK の段 S1)

12:00 UTC 頃に API の時刻とジョブのログから取った。生データは残していない (再取得は同じ手順: スキル os32-ci-inventory)。

標本 (完了した回だけ。実行中の 44298430149・44297850443 などは除外):
- check push 20 回: 44268941385 (11-18 08:39) 〜 44293807742 (11:20)。うち main 5 回・枝 15 回
- build push 20 回: 44253143264 (06:41) 〜 44293807601 (11:20)
- check の dispatch (S0 以外) 20 回: **全部 11-14** (43554414223 〜 43617404157)。ppc-qemu を割る前の古い形で、今の形の dispatch は S0 の分しか無い
- build の dispatch 20 回: 43811232447 (11-16) 〜 44290777801 (11-18)。11-16 の 12 回は古い形 (`check-fast` 6 本)、今の形 (`checks` 6 本) は 8 回
- S0 (wt/ci-s0、check の dispatch 20 回): af58ea349 ×10 (44254897577 〜 44254929089) + 1ee8d5090 ×10 (44268976917 〜 44269022922)
- 時刻は API の created_at / started_at / completed_at。「待ち」= started − created (キュー)、「実行」= completed − started。生データは同じ置き場の jobs/*.json・logs/*.log・sel.txt

#### 1. §1 の表に差し替える形

| workflow | ジョブ | 実行の所要 (中央 / 最小〜最大、push 20 回) | キューの待ち (中央 / 最大) | 結果 (push 20 回) |
|---|---|---|---|---|
| check | static-check | 5.2 / 2.4〜14.3 分 (n=20) | 3.1 / 19.4 分 | success 15・failure 5 (全部が文書の検査、下の 1) |
| check | ppc-compile | 中身を回した回 4.4 / 3.3〜10.3 分 (n=13)。範囲の判定で飛ばした回 0.1〜0.2 分 (n=7) | 3.0 / 22.5 分 | success 20 |
| check | ppc-qemu (4 シャード) | 回した回 16.3 / 8.6〜24.4 分 (n=52)。飛ばした回 0.1〜0.3 分 (n=28) | 5.5 / 19.6 分 | success 80 |
| check | ppc-toolchain-cache (main だけ) | 0.2 / 0.2〜0.4 分 (n=5、キャッシュに当たった) | 7.2 / 18.0 分 | success 5・skipped 15 |
| build | artifacts | 2.2 / 1.7〜3.4 分 (n=18) | 1.2 / 29.0 分 | success 18・cancelled 2 |
| build | checks (6 シャード) | 成功した 77 本で 18.0 / 8.1〜41.8 分。シャード別の中央: (0) 25.5・(1) 11.6・(2) 15.3・(3) 23.2・(4) 24.2・(5) 14.6 | 3.4 / 28.6 分 | success 77・failure 6・cancelled 37 |

push から最後のジョブが終わるまで (run の created_at → 最後の completed_at):
- check だけ: 中央 23.2 分・最大 42.8 分 (44274645643) (n=20)。PowerPC を回した 13 回は中央 28.4 分 (17.9〜42.8)、飛ばした 7 回は 2.9〜25.4 分
- build だけ (cancel の無い 13 回): 中央 36.1 分・最大 68.4 分 (44274576966、runner が通信を失った回)
- check と build の両方が最後まで走った push 8 回: 中央 40.7 分・最小 34.3・最大 68.4 分
- 待ちの分: 各 run で最後に始まったジョブの待ちは、check で中央 10.9 分 (最大 23.2)、build で中央 10.4 分 (最大 29.3)。待ちは同じ時間帯に走る枝の数で変わる (runner の同時数の上限、推測)

| 区分 | run の結果 | 所要 |
|---|---|---|
| check の push 20 回 | success 15・failure 5・cancelled 0 | 上の表 |
| build の push 20 回 | success 9・failure 4・cancelled 7 (同じ枝の次の push で打ち切り。当時のビルド workflow の `concurrency: cancel-in-progress`) | 上の表 |
| check の dispatch 20 回 (11-14、古い形) | success 19・cancelled 1 | static-check 4.7 (3.5〜5.7) 分、ppc-compile 1.0 (0.8〜14.6) 分、全体 中央 5.1・最大 14.7 分 |
| build の dispatch 20 回 | success 12・failure 2・cancelled 6 | 今の形の `checks` 17.0 (9.3〜34.6) 分 (n=24)、古い形の `check-fast` 5.5 (3.4〜12.2) 分 (n=70) |
| S0 の dispatch 20 回 (同じ SHA の揺れの確かめ) | success 20 (ジョブも 140 本全部 success、ppc-toolchain-cache は skipped) | 実行は ppc-compile 4.3 (3.2〜8.7)・ppc-qemu 15.7 (12.9〜22.5)・static-check 4.8 (3.7〜13.0) 分。20 本を同時に起動したので待ちが中央 40〜43 分・最大 95 分、全体は中央 63.4・最大 111.2 分 (所要の基準には使えない) |

#### 2. failure の内訳 (1 行ずつ)

check の push (static-check の 5 本。どれも「Documents」の手順で落ち、ほかのジョブは通過):
- 44290719097 (wt/ppc-kapi-fill 1fe0bf26d): `check-docs-tree` — ticket_crosslink 1 / 基準値 0。本物の赤 (枝の文書)
- 44289493633 (wt/ppc-kapi-fill e7d1fde36): `check-docs-orphans` — docs/tasks/ppc/TASK_PPC_KAPI_FILL.md が孤立。本物の赤
- 44274645643・44271594377・44271211395 (wt/ppc-sqlite-stack): `check-docs-line-refs`。本物の赤 (3 回続けて同じ。次の 6ee9a56f6 で緑)

build の push (failure の 6 ジョブ):
- 44274645658 checks (4) (wt/ppc-sqlite-stack 4ca0851fa): `check-docs-line-refs`。本物の赤 (上と同じ)
- 44274576966 checks (4) (**main** 133205ead): 検査の失敗ではない。`make check` の途中で 53 分走って「The hosted runner lost communication with the server」(注記)。基盤の失敗。ほかの 5 シャードは通過。原因 (メモリ・CPU の枯渇か) は未確認
- 44255276200 checks (2) (wt/ppc-newlib f99c70501): `check-build-compare-host` (AssertionError、MUTATE RED orphan KILL lost)。checks (0) は `check-sep5-boot-host` (tool invoked: boot)・`check-sep5-sdk-host` ('ppc-entries','sdk')。本物の赤 (枝の変更、次の 601f392d0 で緑)
- 44253254726 checks (2) (wt/ppc-newlib ae9f0cbc2): `check-build-compare-host`・`check-map` (arch-ctx-guest の uncovered dynamic artifact build/out/ppc-user/*.o)。本物の赤
- 44253143264 checks (2) (wt/ppc-km-s2u 9fd5d6435): `check-ppc-input-usb-host` (ホストの gcc -m32 のコンパイルが失敗)。本物の赤 (推測: 次の push 1c7f7e6ae で緑)

build の dispatch:
- 44290777801 checks (3) (wt/ppc-kapi-fill): `check-docs-tree` (ticket_crosslink)。本物の赤
- 43982950103 check-fast (3)・43982002500 check-fast (3)・43878949072 check-fast (4) (11-16、古い形): 落ちた検査は**未確認** (ログを取り切れなかった・make の失敗行が見つからなかった)

揺れ・用意の失敗: 今回の標本では 0。ジョブが 1 分以内に落ちた例も無い。

#### 3. SKIP の行

- check の ppc-qemu: ジョブの要約の表 (`gate.py` が端末にも同じ表を出す) の「SKIP の行がある」は、中身を回した 13 回 × 4 シャード = 52 本で **全部 0**
- check の static-check (ログの数え): 1 回あたり 3 行、通過した 15 回で計 45 行 (落ちた 5 回は Documents で止まり、この手順まで行かない)。`TARGET SKIP (i386-elf-gcc not in PATH)` ×2 (`check-ext2-empty-name-host`・`check-vfs-fd-path-host`)、`ERRNO SKIP (newlib headers not found)` ×1 (`check-vfs-fd-path-host`)。クロスの無いジョブで、ターゲット側の確かめが飛んでいる (G6 の対象)
- check の ppc-compile (ログの数え): 回した 13 回で毎回 11 行 (`SKIP: PPC toolchain unavailable` ×4、`SKIP ppc-sc/ppc-sd/ppc-sh-output:` 各 ×2、`SKIP LAN socket rc=3` ×1)。前後の行が `RED: ignoring OS32_REQUIRE_PPC detected`・`PASS: PPC CI gate matrix`・`RED H:A12 skip-as-pass` なので、試験が SKIP の扱いを確かめるために自分で出す行と見る (推測。本物の SKIP の有無は、検査ごとのログが無いので未確認)
- build の checks (ログの数え、表は無い): 44292264241 の 6 シャードで計 109 行 (16・14・15・16・28・20)、44290272004 の 5 シャードで計 86 行 ((4) は取得せず)。ほとんどが PowerPC の検査: `tools/ppc/test_*.py: unavailable: /home/hight/opt/cross-ppc/bin/powerpc-elf-gcc …` (約 35 本の台本)、`ppc-usb`・`ppc-fb`・`ppc-rh-fb`・`ppc-cpu-nap`・`ppc-link-sh`・`arch-ctx-guest`・`layers-ppc` など。PowerPC の道具の場所が手元の既定値 (`/home/hight/opt/cross-ppc`) のまま runner で探されている。x86 側の物: `lychee が無いのでリンク検査を飛ばします` (1)、`FreeType comparison: freetype-py not importable`、`BDF original comparison`・`float-raster comparison` (osN-tmp の元が無い)、SoftFloat の original hashes、`TARGET arm-none-eabi SKIP`、`SKIP AF_PACKET … fixture` (自分で出す行、gate.md §5 のとおり)。行と検査名の対応は、並列の出力が混ざるので一部しか付けられない
- 副産物: build の checks (1) のログが 162〜333 MB (44290272004・44292264241)。GeForce の host 試験の変異の回が `FAIL !write && (prod.display…)` を 230 万行出している。ログの取得が重い (1 本数分)

#### 4. main への push で PowerPC の段が中身を回すか

- **変更前の実測。** 標本の main の push 5 回 (44290201312・44290142172・44277658303・44276821812・44274576283) は 5 回とも `Select PPC scope` の出力が `skip: x86-only, documentation-only, or empty branch delta`。ppc-compile 0.1〜0.2 分、ppc-qemu 4 本 0.1〜0.3 分 (用意以降の手順は飛ぶ)。ppc-toolchain-cache だけが 0.2〜0.4 分で走る
- 規則 (`ppc_scope.py (旧版 d4d22d1c0028)`): `PPC_ARCH` は push では `auto` (dispatch だけ入力の arch)。`x86` → skip、`ppc`・`both` → 回す、枝の名前が `wt/int-` で始まれば回す。それ以外は `git merge-base origin/main HEAD` から HEAD までの差分のパスを見て、docs/・*.md は飛ばし、共通の置き場 (kernel/・lib/・include/ …)・PowerPC の置き場・`build/*.mk`・決まったファイル (Makefile・検査 workflow など) があれば回す、x86 だけの置き場は飛ばし、知らない置き場は控えめに回す。差分が空なら skip
- main の push は before→HEAD が文書だけなら PowerPC を省く。コードを含む場合は同じ SHA の `wt/int-*` の check の push が成功していれば省き、それ以外は x86 だけの変更でも compile と QEMU 6 本を回す。強制 push・before の欠落時も全部回す。 導入後の main push 20 回で、省略と実行の本数・所要を測る。
- 枝でも飛ぶ例: wt/ppc-kapi-fill の 2 回 (44290719097・44289493633) は差分が x86 と文書だけと判定されて skip

## 5-4. 用意を 1 か所に (2026-11-18、票 TASK_CI_REWORK の段 S2a)

check の ppc-qemu の用意を、手書きの apt の行・build-setup・ppc-setup から手本の `gate-setup` 1 行にした (`2b8097b21`・`91d1335a9`・`ad6e9c6fe`)。

| 何を | 前 (run 44293807742、wt/ppc-newlib-fix 97e182cf0) | 後 (run 44313949414、ad6e9c6fe) |
|---|---|---|
| ppc-qemu の 4 シャードの検査の数 (表の合計) | 1・5・4・2 (12 本、全部通過、SKIP の行 0) | 1・5・4・2 (12 本、全部通過、SKIP の行 0) |
| ppc-qemu の 4 シャードのジョブの実時間 | 18.1・16.2・16.9・15.2 分 | 16.0・14.9・16.6・12.9 分 |
| 見つかった赤 | — | 2 つ。(1) `ppc_ci.py` が新しく読む action のファイルが ppc-compile の検査の covers に無く、`check-map` が止めた (R6 の追従の漏れ、`91d1335a9`)。(2) `gate-setup` のメタパッケージ `qemu-system` が全 arch の QEMU (約 100MB) を連れてきて、遅いミラーで用意が 600 秒を越えた (run 44307127004 の ppc-qemu (2))。要る物 (`qemu-system-ppc`・`qemu-system-data`・`seabios`) を名指しにし、apt に再試行と期限の旗 (`ad6e9c6fe`)。sep も同じ `gate-setup` を使うので sep にも効く |

S2b (`b19526c84`、2026-11-19): ppc-compile の用意も `gate-setup` 1 行に。check・build とも 1 回目で success。ppc-compile の通過の行の数は前 (run 44293807742) と後 (run 44324154783) で同じ 39 (ログの行の数え)。

S2c (`268404985`・`00fd03405`・`e262701b9`、2026-11-19): static-check の apt を `.github/actions/static-setup` に移し、static-check は main でも再利用せずに毎回回す (煙試験、main のキャッシュを育てる)。check・build とも success (e262701b9)。1 回目 (268404985) の build の checks (0) は `check-ppc-realhw-link` の揺れ (試験の時計の期限 10 ms、PowerPC の PM が `wt/ppc-rhlink-clock` で直す)。main へは、取り込み直しの差が文書 4 本だけだったので、決まり (PARALLEL_BRANCHES §3) で CI を省いて早送りした (0e7ee0a46)。

2026-11-19 の手順別上限は、build の `make check` が 60 分（検査本体の実測は 31 分 30 秒、ジョブの最長は §5-1 の 39 分）。前後に runner の負荷・CPU/IO pressure・ディスク使用量を記録する。
check の PPC QEMU はシャードごとに 30 分（用意込みで 21 分 43 秒の例、30 分 18 秒の回も検査本体は 13 分 41 秒）、PPC ビルド・ホスト検査は 12 分（用意込み最長 7 分 06 秒）。
重みを入れ直す前の 8078bf349 では QEMU の検査本体が 25.4 分かかっており、30 分の上限にはシャード間の偏りが再び生じた時の余裕を含む。
static-check の Check map は 5 分、Host tests は 6 分（検査本体の実測は順に 1 分 32 秒、2 分 08 秒）。

## 6. 範囲の外

既存の workflow の書き換え、NP21/W・実機の受入、検査の中身の直し (目印のずれなどは見つけた枝で別のコミットにして直す)。

## 5-9. PowerPC の全部の受入の像の配置の一覧 (2026-11-21、PPC_ROOT_LZ4_DESIGN)

| ジョブ | 回る時 | ふだんの所要 | 上限 |
|---|---|---|---|
| `ppc-layout-inventory` (当時の検査 workflow) | 像の配置に効くファイルを触った push の時だけ (旧手順（4d70e2c233f0 の記録）。kernel.ld・memmap.h・platform_*_layout.h・build/images・ppc-powermac.mk・root の生成の道具・sdk/kapi.json ほか)。当たらない時は SKIP の理由の行 | 約 31 分 (195 像) | 90 分 |

ふだんは代表の 1 検査 `ppc-root-lz4-layout-host` (製品 5 像・最大の受入 3 像・64 KiB の整列の跳ね) だけが ppc-qemu の組で回る。2026-11-22 の 10 組の割り振りと、同日の 8 組の実測 4 回の中央値による所要の見積もりは次のとおり。

| 組 | 検査 | 重みの和 (秒) |
|---|---|---:|
| 0/10 | `ppc-display-guest` | 962.2 |
| 1/10 | `ppc-fb`、`ppc-module-guest`、`ppc-mmu` | 875.6 |
| 2/10 | `ppc-link-sh-run`、`ppc-sysinfo-guest`、`ppc-shm-guest`、`ppc-rh-fb`、`ppc-kboot-console` | 855.4 |
| 3/10 | `arch-ctx-guest`、`ppc-sd`、`ppc-port-guest`、`ppc-sh-output` | 873.2 |
| 4/10 | `ppc-fp-guest`、`ppc-memstat-guest`、`ppc-display-lifecycle`、`ppc-console-cells`、`ppc-sqlite-stack-guest` | 867.2 |
| 5/10 | `ppc-link-sh-flow`、`ppc-reset`、`ppc-kcg-guest`、`ppc-pci-guest`、`ppc-fp-accept-host` | 852.8 |
| 6/10 | `ppc-machcfg-guest`、`ppc-gfx2-guest`、`ppc-surface-guest`、`ppc-font-guest`、`ppc-shell-layout-guest` | 860.5 |
| 7/10 | `ppc-boot-info-guest`、`ppc-mouse-guest`、`ppc-ver-guest`、`ppc-rtc-guest`、`ppc-root-lz4-layout-host` | 859.9 |
| 8/10 | `ppc-db-guest`、`ppc-kapi-v89-guest`、`ppc-usb`、`ppc-path-guest`、`ppc-loop-guest` | 878.7 |
| 9/10 | `ppc-ata-guest`、`ppc-sfs-guest`、`ppc-sd-link`、`ppc-sc`、`ppc-sqlite-static-guest` | 882.7 |

## 5-10. PowerPC の QEMU の組の重みの作り直し (2026-11-24、本線 PM の決定 (c))

上限は組ごとに 30 分のまま (用意 約 3 分を含む)。合格の線は各組 28 分以内 (本線 PM)。標本は `calibrate_weights.py --merge` に、int-ppc-1123h・1123a・1123i の check の 6 回分の `check-times` の中央値。

| 回 | 重み | 組の実時間 (用意込み) | 結果 |
|---|---|---|---|
| 1123h 8232174fa (前) | 11-22 の重み | 最長 29 分 15 秒 (組 2) | 上限の際 |
| 1123i 79a554797 | 作り直し 1 回目 (見込み 22.1〜22.3 分) | 18〜29 分 (組 5・8 が 29 分) | 線を越えた |
| 1123i b5b03dc79 | 同上 | 16〜26 分、組 3 は 30 分で打ち切り (`ppc-deploy-guest` 840 秒・`ppc-kapi-v89-guest` 444 秒・`ppc-gfx2-guest` 295 秒、重みは 624・250・193) | 打ち切り |
| 1123i 7cbf23c48 | 作り直し 2 回目 (見込み 21.9〜22.4 分) | 19〜27 分 | 通過 |

同じ検査が回ごとに 4〜8 割伸びる (`ppc-deploy-guest` の標本は 562〜840 秒)。見込み 22 分 + 用意 3 分では、上限までの余裕は 5 分ほど。再び 28 分を越えたら、組の数か上限の見直しを本線 PM からユーザーに上げる。

20 組 (2026-11-24 のユーザー決定「20組に」、上限 30 分のまま) の最初の回 (76ec252b8、check 45349078850): ppc-qemu の各組 10〜18 分 (用意込み、組 0 の `ppc-display-guest` 単独が 18 分)、全部 success。runner の空き待ちで組の開始が 07:35〜08:06 にずれ、push から最後のジョブまでは 44 分 (10 組の時は 30〜36 分)。組 0 が通った回の実測で 25 分を越えたら、`ppc-display-guest` の中を割る案 (変異・待ちの見直し、または 2 本に分ける) を出す (本線 PM、11-24)。

2026-11-25: int-ppc-1124d の回 (e62bca29f、check 45427776109) で組 0 の `ppc-display-guest` 単独が 1785 秒 (ステップの上限 30 分まで 15 秒、18 分の回もあり回ごとの振れが大きい)。本線 PM と決めて 2 本に分けた: 旧手順（4d70e2c233f0 の記録） が normal と変異の場合 18 の 1 つおき (9 ずつ) を回し、`ppc-display-guest` (0/2) と `ppc-display-guest-b` (1/2)。変異の集合・covers・basis は同じ (関門を弱めない)。normal のビルドと起動は 2 本とも要る。重みは仮に 950 秒ずつ、次の回の check-times で `calibrate_weights.py --merge` して作り直す。
