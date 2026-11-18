---
name: os32-ci-read
description: GitHub の CI (check・build・sep) の結果を読む・落ちた検査を特定する・回し直す・起動しない時を直す。push の後に CI を待つ時、赤が出た時、CI が 1 本も起動しない時、API の上限に当たった時に使う。取り込みそのものは os32-land。
---

# os32-ci-read — CI の結果を読む

決まりは [POLICY_CI](../../../docs/POLICY_CI.md)。workflow の形は [08_build](../../../docs/08_build.md) §8-6。

## 1. 起動を確かめる (push の直後に 1 回)

```bash
gh run list --branch <枝> --limit 4 --json name,event,status,headSha -q '.[] | "\(.name) \(.event) \(.status) \(.headSha[0:9])"'
```

`check`・`build` (`wt/sep-*` の枝は `sep` も) が、push した SHA で出ていること。出ていない時:

| 状態 | 理由 | 直し方 |
|---|---|---|
| 1 本も無い | コミットの本文に CI を飛ばす印 (角括弧つきの skip ci) の文字列がある。説明として書いても飛ぶ | 印を含まないコミットを足す (空のコミットでよい) |
| build だけ無い | 変更が文書だけ (build.yml の paths-ignore)・空のコミット・同じ木の force-push | `gh workflow run build.yml --ref <枝>` |
| 古い SHA のまま | 続けて push した。古い実行は打ち切られる | 最後の SHA の行を待つ |

## 2. 待つ

`gh run watch` を何本も回さない (3 秒ごとに問い合わせて、API の上限 1 時間 5,000 回を使い切る)。裏で 5 分おきに 1 回:

```bash
SHA=<push した最後の SHA の頭 9 桁>
for i in $(seq 1 40); do
  out=$(gh run list --branch <枝> --limit 8 --json name,status,conclusion,headSha \
        -q ".[] | select(.headSha|startswith(\"$SHA\")) | \"\(.name) \(.status) \(.conclusion)\"")
  [ "$(echo "$out" | grep -c .)" -ge 2 ] && ! echo "$out" | grep -q "in_progress\|queued\|pending\|waiting" && break
  sleep 300
done; echo "$out"
```

- 待ちの終了と merge・push を 1 本のコマンドに繋げない。結論を読んでから取り込む。
- 所要の目安 (2026-11-18): check の static-check 4〜6 分・ppc-compile 4〜21 分・ppc-qemu (4 シャード、割る前は 1 本で 53〜61 分)、build の checks 6 シャード 10〜39 分。伸びてきたらスキル os32-ci-inventory。
- 上限に当たったら `gh api rate_limit -q '.resources.core'` の `reset` (UNIX 秒) まで待つ。

## 3. 赤を読む

```bash
gh run view <run> --json jobs -q '.jobs[] | "\(.name) \(.conclusion) \(.startedAt[11:16])-\(.completedAt[11:16])"'
```

| ジョブ | 落ちた検査の探し方 |
|---|---|
| build の `checks (i)` | `jid=$(gh run view <run> --json jobs -q '.jobs[] \| select(.name=="checks (i)") \| .databaseId')`、`gh api repos/<owner>/<repo>/actions/jobs/$jid/logs > log`、`grep -n '\*\*\* \[.*check-' log` (検査の名前)、その行の 30 行前までが失敗の本文 |
| check の `static-check`・`ppc-compile` | 同じくジョブ単位でログを取り、`grep -n 'Traceback\|##\[error\]' log`。最初の失敗で止まるので、後ろの検査は回っていない |
| check の `ppc-qemu (i)` | ジョブの要約の表 (失敗・回っていない・遅い検査)。ログは `gh run download <run> -n ppc-qemu-failed-<i>` → `results.json` と `failed-logs/<検査>.log` |
| sep の `checks` (手動) | `gh run download <run> -n sep-checks-<i>` → `results.json` の `status: fail` の行と `failed-logs/<検査>.log` |

- `gh run view --log-failed` が空を返す事がある。ジョブ単位の API (`actions/jobs/<id>/logs`) で取る。
- ジョブの所要が 1 分以内なら、手順が飛ばされていないか `--json jobs -q '.jobs[].steps[]'` で確かめる (範囲の判定で中身が回らない事がある)。変異が回ったかは、ログの `RED` の行の数で分かる。
- 変異込みで初めて見える赤の型: 変異の時だけ作る写しの木にディレクトリ・ヘッダが足りない、置き場の移動で走査の根 (`tools/source_roots.py`) から外れて違反を検出しなくなった、変異の試験だけが要る道具がジョブに無い、ソースを特定のコミットにバイトで固定している試験。

## 4. 手元で再現する

```bash
MUTATE=1 make check-<名>                      # 1 本を変異込みで
python3 -B tools/checks/run.py --select --files <触ったファイル…>   # 触ったファイルを読む検査の一覧
```

- 手元の作業木に `.env` が無ければ `CROSS_DIR=/home/hight/opt/cross` (PowerPC は `PPC_CROSS_DIR=/home/hight/opt/cross-ppc`) を付ける。
- 関係する検査を型の名前で選ぶと漏れる。`--select --files` の一覧から選ぶ (全部は回さない。全体は CI)。
- `manifests` などの鮮度の検査は、`make all` をし直してから。

## 5. 揺れる検査

回し直し (`gh run rerun <run> --failed`) で通したら、検査の名前・落ち方・日付を 持越しの台帳 `docs/tasks/DEFERRED_TESTS.md` に行で残す。同じ検査の 2 回目が出たら、直すまで行を閉じない。
