---
name: os32-ci-read
description: restructure-build の候補 run を確認し、失敗の原因を調べて回し直す。
---

# os32-ci-read — CI の結果を読む

## 候補の run を待つ

`gh run list --workflow restructure-build.yml --branch "$branch" --commit "$sha" --event push` で候補の完全 SHA に一致する run ID を取る。見つからなければ push 先と SHA を確かめる。その 1 run を `gh run watch "$run_id" --interval 300 --exit-status` で待つ。

## 失敗を調べる

`gh run view "$run_id" --json attempt,jobs` で最新 attempt と落ちたジョブを特定し、`gh run view "$run_id" --log-failed` で登録 ID と失敗理由を読む。手元では `python3 -B -m tests.infra.ci run --check <id>` を回す。変異の再現には `--mutate` を付ける。回し直したら同じ run の最新 attempt を確認し、その成果物で計画の各機種を起動し直す。

候補 SHA を `wt/int-*` に push し、`restructure-build` の同じ run の最新 attempt が選択計画の集計まで成功したことを確かめる。文書だけの変更も、その候補の PC-98 と PowerMac の起動記録を揃えて `python3 -B tools/ci/land_gate.py <SHA> --evidence <JSON>` を回す。 JSON の例は [os32-land](../os32-land/SKILL.md)。古い検査は P7-2 で消した。

取り込み前に最新 attempt の plan.json で main の基点、対象の媒体・検査、対象外の理由を確認する。plan が欠けた run は取り込めない。
