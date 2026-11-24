# TASK_CI_REWORK — 既存の CI (check・build) を手本の形に直す

> 状態: **計画 (2026-11-18)** — 本線の再開の最初の仕事 (ユーザー決定 2026-11-18「本線が再開するのは旧い CI の手直しから」)。票は特務の PM が実測から書いた。**S0 は完了** (本線の PM、2026-11-18。原因と直し、同じ SHA の 10 回の結果は [gate.md](../../design/tools/ci/gate.md) §5-2、基準 (5) を満たした)。**S1 は完了** (§1 の表、[gate.md](../../design/tools/ci/gate.md) §5-3)。**S2 は完了** (S2a・S2b・S2c、2026-11-18〜19、gate.md §5-4。check.yml の apt-get は 0 行 = 完了の基準 (2) の check.yml の分)。S3 以降は未着手。
> 完了の基準: (1) `.github/workflows/check.yml` に `make check-<名>` の手書きの行が 0 (今 31 行)、(2) workflow の中の `apt-get install` が 0 行 (今 3 行。用意は composite action だけ)、(3) わざと 2 本落とした枝で、check・build のどのジョブも残りを最後まで回し、落ちた 2 本の名前がジョブの要約の表に出る、(4) 置き換えの前後で回る検査の名前の集合が減っていない事を機械で確かめた記録がある、(5) 揺れた検査 2 本 (`ppc-sd-link`・`ppc-usb`) が同じ SHA の 10 回の実行で 10 回通り、用意の手順も 10 回落ちない、(6) 道具が無い時に SKIP で通る検査が 0。6 つを満たした SHA が main に在る。

決まりは [POLICY_CI](../../POLICY_CI.md) (作る時 G1〜G13、改修する時 R1〜R8)。手本の形と、11-17 の欠点の実測は [gate.md](../../design/tools/ci/gate.md)。この票は、11-18 に関門を変異込みへ切り替えた後に残っている分を、段に切る。

## 1. 今の状態 (2026-11-18 の S1 の実測、push 20 回。詳細と実行の番号は [gate.md](../../design/tools/ci/gate.md) §5-3)

| workflow | ジョブ | 実行の所要 (中央 / 最小〜最大) | キューの待ち (中央 / 最大) | 結果 (push 20 回) | 形 |
|---|---|---|---|---|---|
| check | static-check | 5.2 / 2.4〜14.3 分 | 3.1 / 19.4 分 | success 15・failure 5 (全部が文書の検査の本物の赤) | `make check-<名>` を 31 行、手で並べている。1 行落ちると後ろは回らない |
| check | ppc-compile | 回した回 4.4 / 3.3〜10.3 分 (n=13)、飛ばした回 0.1〜0.2 分 | 3.0 / 22.5 分 | success 20 | `run.py --run all --env ppc-cross`。最初の失敗で止まる |
| check | ppc-qemu (4 シャード) | 回した回 16.3 / 8.6〜24.4 分 (n=52) | 5.5 / 19.6 分 | success 80 | `gate.py run --env qemu-ppc --shard i/4`。表と検査ごとの所要を出す |
| build | artifacts | 2.2 / 1.7〜3.4 分 | 1.2 / 29.0 分 | success 18・cancelled 2 | — |
| build | checks (6 シャード) | 18.0 / 8.1〜41.8 分。シャード別の中央 (0) 25.5・(1) 11.6・(2) 15.3・(3) 23.2・(4) 24.2・(5) 14.6 | 3.4 / 28.6 分 | success 77・failure 6 (本物の赤 5・runner が通信を失った 1)・cancelled 37 | `make -k check CHECK_SHARD=i/6`。1 本落ちても残りを回す |
| sep (`wt/sep-*` だけ) | identity・checks | 19 分・37〜57 分 (11-17) | — | — | 手本 (`tools/ci/gate.py`) |

push から最後のジョブが終わるまで: check と build の両方が最後まで走った 8 回で中央 40.7 分・最大 68.4 分。各回で最後に始まったジョブのキューの待ちは中央 10〜11 分・最大 23〜29 分 (同じ時間帯に走る枝の数で変わる)。揺れ・用意の失敗は S1 の標本で 0。

残っている欠点と、11-18 に実際に起きた事:

| 決まり | 今の状態 | 11-18 の実害 |
|---|---|---|
| G2 全部回して全部挙げる | check の static-check・ppc-compile は最初の失敗で止まる (ppc-qemu は 11-18 に直した) | ppc-compile が 1 本で落ち、ppc-qemu は走らなかった。直して回し直し (1 往復 50 分) |
| G3 結果は表で | check は表が無い。落ちた検査は `gh run view --log-failed` を読んで探す | 失敗の行を探すのに、ログを 3 回取り直した |
| G4 何を回すかは登録から | static-check は手書きの 31 行。`tools/ci/ppc_scope.py` は共通・PowerPC の置き場の一覧を自前で持つ | 新しい検査 2 本 (`archive-refs`) を手で足した。足し忘れると build でしか回らない |
| G5 用意は 1 か所 | `apt-get install` が check.yml に 3 行、中身が違う | ppc-compile にだけ `qemu-system-ppc` が無く、変異の試験 (`tools/ppc/test_lan_p29.py`) が落ちた |
| G6 道具が無い時に通さない | クロスコンパイラの無いジョブでは検査が SKIP で通る | S1 の実測: build の checks は 1 回で SKIP の行 86〜109 行。大半は PowerPC の台本約 35 本が道具を手元の既定の置き場 `/home/hight/opt/cross-ppc` に探して飛ばしている。ほかに lychee・freetype-py が無くて飛ぶ行。static-check は毎回 3 行 (`check-ext2-empty-name-host`・`check-vfs-fd-path-host` のターゲット側の確かめ)。ppc-qemu の表の SKIP は 52 本とも 0 |
| G7 CI と手元で同じ入口 | `make check-…` と `run.py --run …` の 2 つ | 11-18 は出ていない (11-17 に `r1-fixture-host` が片方だけで落ちた) |
| 揺れる検査 | 無し (11-18 の S0 で 2 本とも原因が分かった)。`ppc-sd-link` は揺れではなかった: 11-17 の回 (枝 wt/sep-ci の手本の CI の最初の回) は用意に `qemu-system` (mac99 の VGA の ROM) が無く、PowerPC の QEMU の検査 11 本が起動直後の QMP EOF で落ちていた (`boot1 READY` の待ちには入っていない)。`50da7775f` で直っている。用意の rustup は、runner が rust-src の登録の無い同じ版の toolchain を持って始まる型 (直近 49 ジョブ中 1 本) | 用意は壊れた toolchain を消してから入れ直す形に直した (S0)。詳細は [gate.md](../../design/tools/ci/gate.md) §5-2 |
| 揺れを直した検査 | `ppc-usb` (「HOME/LEFT/RIGHT/DEL result」、11-17 に 2 回・11-18 に 1 回) | 原因は試験の待ちの条件: DELETE で行を描き直すと出力の末尾がプロンプトになるので、Enter の結果が出る前に待ちが終わっていた (`userland/shell/ui.c` の `redraw_line` は最後にプロンプトを出し直す)。結果の行が出るまで待つ形に直した (`tools/ppc/usb_run.py`、11-18。履歴の試験と同じ型の直し) |

| ログの大きさ | build の checks (1) のログが 162〜333 MB (S1)。GeForce の host 試験の変異の回が同じ FAIL の行を約 230 万行出す | ログの取得に 1 本数分かかる。G3 (落ちた検査だけのログ) の段 S5 で、変異の回の出力を検査ごとのログに閉じる |
| 基盤の失敗 | main の build の checks (4) が 53 分走って「The hosted runner lost communication with the server」(run 44274576966)。原因 (メモリ・CPU の枯渇か) は未確認 | 1 回。2 回目が出たら持越し台帳に行を立てて調べる |

関門の外で CI が起動しない条件 (直す対象ではなく、取り込みで踏む所): コミットの本文に CI を飛ばす印の文字列があると全部飛ぶ、文書だけ・空のコミットの push では build が起動しない、main への push では PowerPC の段は中身を回さない (S1 で確かめた: `tools/ci/ppc_scope.py` は `merge-base(origin/main, HEAD)` からの差分を見るので、main では差分が空になり必ず skip。PowerPC の検査は取り込む前の枝の push で回り、main へは同じ SHA の早送りなので関門としては欠けない。main で回すのは arch=ppc/both の dispatch だけ)。

## 2. 段

1 コミットで替えるのは 1 つの workflow の 1 つのジョブまで (R5)。各段は枝 → CI → main。workflow を読む試験 (`tools/tests/test_ci_split.py`・`ppc_ci.py`・`test_ci_gate.py`) と文書を同じ変更で直す (R6)。置き換えた古い行・道具・試験は同じ変更で消す (R7)。

| 段 | 中身 | 決まり | 受入 |
|---|---|---|---|
| S0 | 揺れる検査の原因を調べて直す (`ppc-sd-link` と用意の rustup。`ppc-usb` は 11-18 に直したので、10 回の確かめだけ)。調べ方: 同じ SHA で `gh workflow run` を 10 回、落ちた回の QEMU のログ (成果物) を読む。期限の値を延ばすだけの直しはしない (何を待っているかを突き止める) | R4 | 基準 (5) |
| S1 | 実測を取り直して §1 の表を更新する: 直近 20 回の成功率・ジョブごとの所要・SKIP の行の数・main への push で PowerPC の段が回る条件 | R1 | §1 の表に数字と実行の番号 |
| S2 | 用意を 1 か所に: check.yml の `apt-get install` 3 行を composite action (`build-setup`・`ppc-setup`・`gate-setup`) に寄せ、PowerPC の 2 ジョブを同じ環境にする | G5 | 基準 (2)。前後で回る検査の集合が同じ |
| S3 | 入口を 1 つに: check の 3 ジョブの `run:` を、手本と同じ道具 (`tools/ci/gate.py`) の 1 行にする。手元でも同じコマンドで回る | G7 | 手元と CI で同じ結果の表 |
| S4 | 一覧を登録から: static-check の 31 行を、検査の宣言 (`CHECK` の tier・環境) から引く。`ppc_scope.py` の置き場の一覧を所有者集合 (`build/select.mk`) から引く | G4 | 基準 (1)・(4) |
| S5 | 全部回して表に: check の残りの 2 ジョブ (static-check・ppc-compile) を、1 本落ちても残りを回す形に (ppc-qemu は済み)。`needs` の鎖は成果物を読む所だけ残す。落ちた検査の名前・末尾・遅い検査をジョブの要約に出す | G2・G3・G10 | 基準 (3) (G12 の実演の枝を 1 本作る) |
| S6 | SKIP で通さない: 道具が無ければ検査を始める前にまとめて挙げて終了コード 2。SKIP の行のある検査を結果の表に挙げる。QEMU を要る検査 12 本は x86 のシャード (build の `checks`) から外す — 今はゲストを起動せずに 1〜2 秒で通り、割り振りでは重みを持つので x86 のシャードを偏らせている ([gate.md](../../design/tools/ci/gate.md) §5-1 の 3) | G6 | 基準 (6) |
| S7 | x86 のシャードの重みを CPU 時間で測る: `tools/check_time.py` が検査の子プロセスの CPU 時間 (user + sys) を記録に足し、`calibrate_weights.py` がそれを重みにする。記録の秒は並んで回る検査に左右され、中央値で作り直しても揃わなかった (11-18、最大 ÷ 最小が 1.92 → 3.25 で戻した) | G10 | build の 6 シャードの実時間の 最大 ÷ 最小 が 1.3 以下 |

順の理由: S0 を先にするのは、揺れがあると後の段の CI の赤が「段のせいか揺れか」を毎回調べる事になるため。S2・S3 は回る検査の集合を変えないので先に替えられる。S4 は集合を変える恐れがあるので、S3 で入口が 1 つになってから。

## 2-0. 見つけた改善 (段とは別)

- `tools/build_compare.py` が `images/os32_install.iso` を ISO の記述子とディレクトリの記録の日時を揃えずに比べるので、中身が同じでも毎回差が出て終了コード 1 になる (2026-11-19、KAPI の 1a・2a-i・6e で毎回)。ISO の中のファイルの一覧と中身で比べる (isoinfo など) か、日時を固定して作る。

## 2-1. 棚卸し (段とは別に、節目ごと)

CI の待ち時間は、検査が増えるたび・変異が増えるたびに伸びる。スキル `.claude/skills/os32-ci-inventory` の手順で、検査ごとの所要・シャードの偏り・重みのずれを測り、重み (`tools/checks/weights.json`) を作り直す。する時: この票の段を 1 つ入れた後、検査を大きく足した後、待ち時間が前の棚卸しから 2 割以上伸びた時。結果は [gate.md](../../design/tools/ci/gate.md) §5-1 に足す。

ユーザー決定 (2026-11-21):
- check の PowerPC の QEMU の段は **20 本** (2026-11-24「20組に」。2026-11-22 の「組分けを 10 組で再構成」を改める)。10 組では重みを 2 回作り直しても、同じ検査が回ごとに 4〜8 割伸びて 30 分の時間切れが残った (gate.md §5-10)。上限は組ごとに 30 分のまま。行列は `[0..19]`、`--shard i/20`、x86 の build の `checks` は 6 組のまま。§1 の表の「4 シャード」は 11-18 の S1 の実測の時の形。
- KAPI v89 の後に棚卸しをする (「かなりの回収なので CI がある程度長いのは仕方ありませんが、通った後は棚卸しをした方が良いでしょうね」)。外す・軽くする検査はユーザーが決める (POLICY_CI G14)。2026-11-21 に PM が重みだけを実測 3 回の中央値で作り直した (QEMU の 6 組の見込み 1,308〜1,344 秒、main e565f1d5a)。外す・軽くする検査も 2026-11-22「両方良きように」で PM に任された — 10 組で待ち時間が収まるかを測ってから決め、それまでは検査を外さない (上位 15 本の表は PM の控え ~/os32-tmp/run/ci-rebal-1121/TOP15.md)。

## 3. やらない事

- sep.yml を消して check・build に畳む事 (分離の枝専用の関門は、分離の仕事が終わるまで残す)。
- x86 の起動を CI で自動に確かめる関門 (NP21/W は CI に載らない。別の票で扱う)。
- 関門を弱める変更 (シャードから検査を外す・変異を外す・期限を延ばして通す)。外す検査が出たら、名前・理由・直す票をこの票の §1 に挙げ、空になったら消す (R4)。

## 4. 記録

段ごとに、替える前と後の所要時間・回った検査の数・見つかった赤を [gate.md](../../design/tools/ci/gate.md) に書く (R8)。段の状態はこの票の冒頭の状態行。
