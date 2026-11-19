---
name: os32-ci-inventory
description: CI の待ち時間の棚卸し。検査ごとの所要・シャードの偏り・重みのずれを成果物の記録から測り、重みを作り直してシャードを揃える。CI が遅くなったと感じた時、検査を大きく足した・変異を足した後、節目 (月に 1 回・大きな取り込みの後) に使う。赤の読み方は os32-ci-read、CI の作りを変える決まりは POLICY_CI。
---

# os32-ci-inventory — CI の所要の棚卸し

決まりは [POLICY_CI](../../../docs/POLICY_CI.md) G10 (時間を割り、測り、見えるようにする)、道具の設計は [time_inventory](../../../docs/design/tools/ci/time_inventory.md)。待ち時間は一番遅いジョブで決まる。シャードの割り振りは検査ごとの重み (`tools/checks/weights.json`) で釣り合いを取るので、重みが実態とずれると遅くなる。

## 0. 基準 (前の棚卸しの値)

比べる相手は `docs/design/tools/ci/gate.md` §5-1 の最後の行。2026-11-18 の棚卸しの後の値:

| 項目 | 値 |
|---|---|
| push から最後のジョブが終わるまで | 30〜36 分 (2 回の実測。build の一番遅いシャードが決める) |
| check の全体 | 17〜22 分 (`ppc-qemu` の 4 本は 14〜22 分、`ppc-compile` 4〜7 分、`static-check` 5〜7 分) |
| build の `checks` 6 本 | 11〜36 分 (回ごとに一番遅いシャードが 30 分と 36 分) |
| QEMU の組の偏り | 1.17 |
| x86 の組の偏り | 2.76 (揃っていない。票 `docs/tasks/v3/TASK_CI_REWORK.md` の段 S6・S7 が済むまでは既知) |

棚卸しをする目安: 全体が基準から 2 割以上伸びた (続けて 43 分を超えた)、QEMU の組の偏りが 1.5 を超えた、検査を大きく足した・変異を足した、票の段を 1 つ入れた。

## 1. 測る

成功した回を 3〜5 回分 (main か、取り込んだ枝の回)。失敗した回は使わない (落ちた検査の所要は途中まで)。

```bash
gh run list --limit 60 --json name,headSha,databaseId,headBranch,conclusion,createdAt \
  -q '.[] | select(.conclusion=="success" and (.name=="build" or .name=="check")) | "\(.databaseId) \(.name) \(.headBranch) \(.headSha[0:9]) \(.createdAt[0:16])"'
D=~/os32-tmp/run/ci-inventory/$(date +%Y%m%d); mkdir -p $D
for id in <build と check の RUN_ID…>; do gh run download $id -p 'check-times-*' -D $D/$id; done     # 置き場は -D で渡す
python3 -B tools/ci/time_inventory.py $D        # 表と判定。終了コード 1 = 手当てが要る
```

ジョブの実時間 (用意とビルドを含む) は記録に無いので、別に読む:

```bash
gh run view <RUN_ID> --json jobs -q '.jobs[] | "\(.name) \(.conclusion) \(.startedAt[11:16])-\(.completedAt[11:16])"'
```

- main への push の回は、PowerPC の段が範囲の判定で中身を回さない事がある (1 分以内で終わる)。QEMU の段の記録は、枝の push の回から取る。
- 同じ SHA の枝の回と main の回は、成果物の名前が同じ。回ごとに置き場を分ける (上の `$D/$id`)。

## 2. 読む

| 表 | 見る所 | 目安 |
|---|---|---|
| シャードの表 | 偏り (検査の秒の和の 最大 ÷ 最小) | 1.5 を超えたら重みを作り直す |
| 重い検査 | 上位 15 本と、全体に占める割合 | 1 本が 1 シャードの和の半分を超えたら、割っても縮まない (§4) |
| 重みのずれ | 実測と重みが 2 倍以上違う検査、重みの無い検査 | 1 本でもあれば作り直す |

検査の秒は、同じシャードで並んで回るほかの検査に CPU を取られた分を含む。同じ検査でも回ごとに ±5 割ほど動くので、1 回の値で細かく合わせない。

**道具が「重みを作り直す」と判定しても、x86 の組だけが理由なら作り直さない** (段 S6・S7 が済むまで)。道具は x86 の組が在ると注意を出す。QEMU の組の偏りと、QEMU の検査の重みのずれが理由の時は §3 へ。

## 3. 重みを作り直す

```bash
python3 -B tools/checks/calibrate_weights.py --merge $D/<check の回>    # QEMU の検査の重みだけ更新 (ほかは残す)
python3 -B tools/checks/calibrate_weights.py $D          # 全部を書き直す (中央値)。x86 は下の注意を読んでから
for i in 0 1 2 3 4 5; do python3 -B tools/checks/run.py --list --shard $i/6 | wc -w; done     # x86 の割り振り
for i in 0 1 2 3; do python3 -B tools/checks/run.py --list --env qemu-ppc --shard $i/4; done    # QEMU の段の割り振り
MUTATE=1 make check-check-select-host check-ci-time-inventory-host check-ci-gate-host
```

- **QEMU の段と x86 のシャードで、秒の意味が違う。** QEMU の段はシャードの中が直列なので、秒がそのまま重みになる。x86 のシャードは検査を並列に回すので、秒は並んで回るほかの検査に左右される。2026-11-18 に x86 の重みを秒の中央値で全部作り直したら、偏りが 1.92 → 3.25 に悪くなって戻した。x86 を作り直す時は必ず前後の実時間で判断する (CPU 時間で測る形にするのは票 `docs/tasks/v3/TASK_CI_REWORK.md` の段 S7)。
- **300 秒を越える検査は、中を並列にできるかを表に残す** (POLICY_CI G13、2026-11-19)。棚卸しの表に「変異・場合の繰り返しが直列か、`mutpar.run_ordered` に載っているか、直列に残す理由」の列を持つ。最初の表は `~/os32-tmp/run/mut-par/inventory.tsv` (17 本)。
- QEMU を要る検査は QEMU の段の記録だけを標本にする (x86 のシャードでは名前だけ回って 1〜2 秒で終わる)。道具が宣言の `env` を見て分ける。
- 変更は `tools/checks/weights.json` の 1 本。枝を push して CI を回し、**前と後のジョブの実時間を比べる**。縮まなければ戻す。前後の数字は `docs/design/tools/ci/gate.md` に書く ([POLICY_CI](../../../docs/POLICY_CI.md) R8)。
- **割り振りを変えると、同じシャードで並ぶ検査の組が変わる。** 前から在った競合が表に出る事がある (2026-11-18: 変異の試験が作業木の根に残したコアファイルを、同時に走った `check-map` が拾って落ちた)。落ちたら割り振りのせいにして戻さず、競合の方を直す。
- 回る検査の集合は変わらない (割り振りだけが変わる)。確かめ方: 作り直す前後で `python3 -B tools/checks/run.py --list | sort` が同じ。

## 4. 割り振りで縮まない時

| 状態 | 手当て |
|---|---|
| どのシャードも長い (和が均等で、全部が遅い) | シャードの本数を増やす。`build.yml` の行列 (x86 は 6) か `check.yml` の `ppc-qemu` の行列 (4) と、道具に渡す `i/N`、それを固定している試験 (`tools/tests/test_ci_split.py`・`ppc_ci.py`) を同じ変更で直す |
| 1 本の検査が飛び抜けて重い | その検査の変異の本数と 1 本の所要を見る (`MUTATE=1 make check-<名>` を手元で回し、ログの `RED` の行と時刻)。QEMU を 1 本ごとに立ち上げ直す・時間切れを待って検出する変異が重い |
| QEMU の段の 1 本が長い | `tools/ppc/` の道具の待ち (期限の値・画面を流す行数) を見る。期限を延ばして通す直しはしない |
| 用意 (道具の導入・クロスコンパイラのビルド) が長い | ジョブのログで `Cache not found` を探す。キャッシュは枝ごとで、枝が読めるのは自分の枝と main の物だけ — main に無いキャッシュは、新しい枝の最初の回で毎回作り直しになる。main で保つジョブ (`check.yml` の `ppc-toolchain-cache`) が要る物が増えていないか見る。用意の行の重複は票 `docs/tasks/v3/TASK_CI_REWORK.md` の段 S2 |

ジョブの間の `needs` の鎖を外す時は、前のジョブがキャッシュを作る役を兼ねていないかを先に見る (2026-11-18: `ppc-qemu` の鎖を外したら、4 本がそれぞれクロスコンパイラを組んで 13〜15 分ずつ伸びた。main でキャッシュを保つジョブを足して解いた)。

関門を弱める手当て (検査を外す・変異を外す・`needs` で後ろを飛ばす) はしない ([POLICY_CI](../../../docs/POLICY_CI.md) R3)。

## 5. 記録

棚卸しの結果 (日付・偏り・重い検査の上位・した手当て・前後の実時間) を `docs/design/tools/ci/gate.md` の実測の節に足す。成果物の控えは `~/os32-tmp/run/ci-inventory/<日付>/` (リポジトリには入れない)。
