# TASK_CI_REWORK — 既存の CI (check・build) を手本の形に直す

> 状態: **計画 (2026-11-18)** — 本線の再開の最初の仕事 (ユーザー決定 2026-11-18「本線が再開するのは旧い CI の手直しから」)。票は特務の PM が実測から書いた。段は未着手。
> 完了の基準: (1) `.github/workflows/check.yml` に `make check-<名>` の手書きの行が 0 (今 31 行)、(2) workflow の中の `apt-get install` が 0 行 (今 3 行。用意は composite action だけ)、(3) わざと 2 本落とした枝で、check・build のどのジョブも残りを最後まで回し、落ちた 2 本の名前がジョブの要約の表に出る、(4) 置き換えの前後で回る検査の名前の集合が減っていない事を機械で確かめた記録がある、(5) 揺れた検査 2 本 (`ppc-sd-link`・`ppc-usb`) が同じ SHA の 10 回の実行で 10 回通る、(6) 道具が無い時に SKIP で通る検査が 0。6 つを満たした SHA が main に在る。

決まりは [POLICY_CI](../../POLICY_CI.md) (作る時 G1〜G12、改修する時 R1〜R8)。手本の形と、11-17 の欠点の実測は [gate.md](../../design/tools/ci/gate.md)。この票は、11-18 に関門を変異込みへ切り替えた後に残っている分を、段に切る。

## 1. 今の状態 (2026-11-18 の実物と実行)

| workflow | ジョブ | 所要 (11-18 の 3 回) | 形 |
|---|---|---|---|
| check | static-check | 4〜6 分 | `make check-<名>` を 31 行、手で並べている。1 行落ちると後ろは回らない |
| check | ppc-compile | 4〜21 分 (キャッシュの有無) | `run.py --run all --env ppc-cross`。最初の失敗で止まる |
| check | ppc-qemu (4 シャード) | 1 本で直列の時は 53〜61 分。2026-11-18 に 4 本に割った (実測は [gate.md](../../design/tools/ci/gate.md) §5-1) | `python3 -B tools/ci/gate.py run --env qemu-ppc --shard i/4`。`needs` の鎖は無い。1 本落ちても残りを回し、表と検査ごとの所要を出す |
| build | artifacts | 2〜3 分 | — |
| build | checks (6 シャード) | 10〜39 分 | `make -k check CHECK_SHARD=i/6`。1 本落ちても残りを回す (G2 は済み) |
| sep (`wt/sep-*` だけ) | identity・checks | 19 分・37〜57 分 | 手本 (`tools/ci/gate.py`)。結果の表・落ちた検査だけのログ・検査ごとの秒 |

残っている欠点と、11-18 に実際に起きた事:

| 決まり | 今の状態 | 11-18 の実害 |
|---|---|---|
| G2 全部回して全部挙げる | check の static-check・ppc-compile は最初の失敗で止まる (ppc-qemu は 11-18 に直した) | ppc-compile が 1 本で落ち、ppc-qemu は走らなかった。直して回し直し (1 往復 50 分) |
| G3 結果は表で | check は表が無い。落ちた検査は `gh run view --log-failed` を読んで探す | 失敗の行を探すのに、ログを 3 回取り直した |
| G4 何を回すかは登録から | static-check は手書きの 31 行。`tools/ci/ppc_scope.py` は共通・PowerPC の置き場の一覧を自前で持つ | 新しい検査 2 本 (`archive-refs`) を手で足した。足し忘れると build でしか回らない |
| G5 用意は 1 か所 | `apt-get install` が check.yml に 3 行、中身が違う | ppc-compile にだけ `qemu-system-ppc` が無く、変異の試験 (`tools/ppc/test_lan_p29.py`) が落ちた |
| G6 道具が無い時に通さない | クロスコンパイラの無いジョブでは PowerPC の検査が SKIP で通る | 11-18 は出ていない |
| G7 CI と手元で同じ入口 | `make check-…` と `run.py --run …` の 2 つ | 11-18 は出ていない (11-17 に `r1-fixture-host` が片方だけで落ちた) |
| 揺れる検査 | `ppc-sd-link` (QEMU の `boot1 READY` の期限切れ、11-17 の変異込みの回で 1 回)・`ppc-usb` (`usb_run.py` の mouse-wait、11-17 に 2 回) | 回し直しで通した。原因は調べていない。台帳 [DEFERRED_TESTS](../DEFERRED_TESTS.md) の `PPC-USB-MOUSEWAIT-FLAKE` |

関門の外で CI が起動しない条件 (直す対象ではなく、取り込みで踏む所): コミットの本文に CI を飛ばす印の文字列があると全部飛ぶ、文書だけ・空のコミットの push では build が起動しない、main への push の PowerPC の段は範囲の判定で中身を回さない事がある (11-18 の main の 2 回は 1 分以内で終わった。意図どおりかは未確認 — 段 S1 で確かめる)。

## 2. 段

1 コミットで替えるのは 1 つの workflow の 1 つのジョブまで (R5)。各段は枝 → CI → main。workflow を読む試験 (`tools/tests/test_ci_split.py`・`ppc_ci.py`・`test_ci_gate.py`) と文書を同じ変更で直す (R6)。置き換えた古い行・道具・試験は同じ変更で消す (R7)。

| 段 | 中身 | 決まり | 受入 |
|---|---|---|---|
| S0 | 揺れる検査 2 本の原因を調べて直す。調べ方: 同じ SHA で `gh workflow run` を 10 回、落ちた回の QEMU のログ (成果物) を読む。期限の値を延ばすだけの直しはしない (何を待っているかを突き止める) | R4 | 基準 (5) |
| S1 | 実測を取り直して §1 の表を更新する: 直近 20 回の成功率・ジョブごとの所要・SKIP の行の数・main への push で PowerPC の段が回る条件 | R1 | §1 の表に数字と実行の番号 |
| S2 | 用意を 1 か所に: check.yml の `apt-get install` 3 行を composite action (`build-setup`・`ppc-setup`・`gate-setup`) に寄せ、PowerPC の 2 ジョブを同じ環境にする | G5 | 基準 (2)。前後で回る検査の集合が同じ |
| S3 | 入口を 1 つに: check の 3 ジョブの `run:` を、手本と同じ道具 (`tools/ci/gate.py`) の 1 行にする。手元でも同じコマンドで回る | G7 | 手元と CI で同じ結果の表 |
| S4 | 一覧を登録から: static-check の 31 行を、検査の宣言 (`CHECK` の tier・環境) から引く。`ppc_scope.py` の置き場の一覧を所有者集合 (`build/select.mk`) から引く | G4 | 基準 (1)・(4) |
| S5 | 全部回して表に: check の残りの 2 ジョブ (static-check・ppc-compile) を、1 本落ちても残りを回す形に (ppc-qemu は済み)。`needs` の鎖は成果物を読む所だけ残す。落ちた検査の名前・末尾・遅い検査をジョブの要約に出す | G2・G3・G10 | 基準 (3) (G12 の実演の枝を 1 本作る) |
| S6 | SKIP で通さない: 道具が無ければ検査を始める前にまとめて挙げて終了コード 2。SKIP の行のある検査を結果の表に挙げる | G6 | 基準 (6) |

順の理由: S0 を先にするのは、揺れがあると後の段の CI の赤が「段のせいか揺れか」を毎回調べる事になるため。S2・S3 は回る検査の集合を変えないので先に替えられる。S4 は集合を変える恐れがあるので、S3 で入口が 1 つになってから。

## 2-1. 棚卸し (段とは別に、節目ごと)

CI の待ち時間は、検査が増えるたび・変異が増えるたびに伸びる。スキル `.claude/skills/os32-ci-inventory` の手順で、検査ごとの所要・シャードの偏り・重みのずれを測り、重み (`tools/checks/weights.json`) を作り直す。する時: この票の段を 1 つ入れた後、検査を大きく足した後、待ち時間が前の棚卸しから 2 割以上伸びた時。結果は [gate.md](../../design/tools/ci/gate.md) §5-1 に足す。

## 3. やらない事

- sep.yml を消して check・build に畳む事 (分離の枝専用の関門は、分離の仕事が終わるまで残す)。
- x86 の起動を CI で自動に確かめる関門 (NP21/W は CI に載らない。別の票で扱う)。
- 関門を弱める変更 (シャードから検査を外す・変異を外す・期限を延ばして通す)。外す検査が出たら、名前・理由・直す票をこの票の §1 に挙げ、空になったら消す (R4)。

## 4. 記録

段ごとに、替える前と後の所要時間・回った検査の数・見つかった赤を [gate.md](../../design/tools/ci/gate.md) に書く (R8)。段の状態はこの票の冒頭の状態行。
