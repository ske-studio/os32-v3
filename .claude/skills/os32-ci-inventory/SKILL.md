---
name: os32-ci-inventory
description: CI の待ち時間の棚卸し。検査ごとの所要・シャードの偏り・重みのずれを成果物の記録から測り、重みを作り直してシャードを揃える。CI が遅くなったと感じた時、検査を大きく足した・変異を足した後、節目 (月に 1 回・大きな取り込みの後) に使う。赤の読み方は os32-ci-read、CI の作りを変える決まりは POLICY_CI。
---

# os32-ci-inventory — CI の所要の棚卸し

決まりは [POLICY_CI](../../../docs/POLICY_CI.md) G10 (時間を割り、測り、見えるようにする)、道具の設計は [time_inventory](../../../docs/design/tools/ci/time_inventory.md)。待ち時間は一番遅いジョブで決まる。シャードの割り振りは検査ごとの重み (`tools/checks/weights.json`) で釣り合いを取るので、重みが実態とずれると遅くなる。

## 1. 測る

成功した回を 3〜5 回分 (main か、取り込んだ枝の回)。失敗した回は使わない (落ちた検査の所要は途中まで)。

```bash
gh run list --limit 60 --json name,headSha,databaseId,headBranch,conclusion,createdAt \
  -q '.[] | select(.conclusion=="success" and (.name=="build" or .name=="check")) | "\(.databaseId) \(.name) \(.headBranch) \(.headSha[0:9]) \(.createdAt[0:16])"'
D=~/os32-tmp/run/ci-inventory/$(date +%Y%m%d); mkdir -p $D
for id in <build と check の RUN_ID…>; do mkdir -p $D/$id; (cd $D/$id && gh run download $id -p 'check-times-*'); done
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

## 3. 重みを作り直す

```bash
python3 -B tools/checks/calibrate_weights.py $D          # tools/checks/weights.json を書き直す (中央値)
for i in 0 1 2 3 4 5; do python3 -B tools/checks/run.py --list --shard $i/6 | wc -w; done     # x86 の割り振り
for i in 0 1 2 3; do python3 -B tools/checks/run.py --list --env qemu-ppc --shard $i/4; done    # QEMU の段の割り振り
MUTATE=1 make check-check-select-host check-ci-time-inventory-host check-ci-gate-host
```

- QEMU を要る検査は QEMU の段の記録だけを標本にする (x86 のシャードでは名前だけ回って 1〜2 秒で終わる)。道具が宣言の `env` を見て分ける。
- 変更は `tools/checks/weights.json` の 1 本。枝を push して CI を回し、**前と後のジョブの実時間を比べる**。縮まなければ戻す。前後の数字は `docs/design/tools/ci/gate.md` に書く ([POLICY_CI](../../../docs/POLICY_CI.md) R8)。
- 回る検査の集合は変わらない (割り振りだけが変わる)。確かめ方: 作り直す前後で `python3 -B tools/checks/run.py --list | sort` が同じ。

## 4. 割り振りで縮まない時

| 状態 | 手当て |
|---|---|
| どのシャードも長い (和が均等で、全部が遅い) | シャードの本数を増やす。`build.yml` の行列 (x86 は 6) か `check.yml` の `ppc-qemu` の行列 (4) と、道具に渡す `i/N`、それを固定している試験 (`tools/tests/test_ci_split.py`・`ppc_ci.py`) を同じ変更で直す |
| 1 本の検査が飛び抜けて重い | その検査の変異の本数と 1 本の所要を見る (`MUTATE=1 make check-<名>` を手元で回し、ログの `RED` の行と時刻)。QEMU を 1 本ごとに立ち上げ直す・時間切れを待って検出する変異が重い |
| QEMU の段の 1 本が長い | `tools/ppc/` の道具の待ち (期限の値・画面を流す行数) を見る。期限を延ばして通す直しはしない |
| 用意 (道具の導入・クロスコンパイラのビルド) が長い | キャッシュの鍵が外れていないか (`actions/cache` のログの `Cache restored`)。用意の行の重複は票 `docs/tasks/v3/TASK_CI_REWORK.md` の段 S2 |

関門を弱める手当て (検査を外す・変異を外す・`needs` で後ろを飛ばす) はしない ([POLICY_CI](../../../docs/POLICY_CI.md) R3)。

## 5. 記録

棚卸しの結果 (日付・偏り・重い検査の上位・した手当て・前後の実時間) を `docs/design/tools/ci/gate.md` の実測の節に足す。成果物の控えは `~/os32-tmp/run/ci-inventory/<日付>/` (リポジトリには入れない)。
