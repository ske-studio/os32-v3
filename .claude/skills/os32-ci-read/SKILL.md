---
name: os32-ci-read
description: GitHub の CI (check・build・sep) の結果を読む・落ちた検査を特定する・回し直す・起動しない時を直す。push の後に CI を待つ時、赤が出た時、CI が 1 本も起動しない時、API の上限に当たった時に使う。取り込みそのものは os32-land。
---

# os32-ci-read — CI の結果を読む

> **作り直しの票の間 (2026-11-26〜P6 の切り替え) の一時の形**: 古い workflow (check・build・sep・selfhost-probe) は止めてあり、CI は `restructure-build` だけ。main に入れる判定は `python3 -B tools/ci/land_gate.py <SHA> --evidence <JSON>` — JSON は候補の SHA・その run と attempt (全部のジョブの success)・PM の機械の基点 (`v3-pre-restructure`) との比べの記録 (`comparison`)・PC-98 と PowerMac の起動の記録 (`boots`、媒体の名前・sha256・run・attempt・artifact id・ログ)。形の例は P3 の `~/os32-tmp/run/restructure/p3-gate/evidence.json`、定めは [ci_v2 §0](../../../docs/design/tools/ci/ci_v2.md)。文書だけの取り込みも起動の記録を取る (DECISIONS の U21)。下の check・build の手順は切り替えの後に書き直す。


決まりは [POLICY_CI](../../../docs/POLICY_CI.md)。workflow の形は [08_build](../../../docs/08_build.md) §8-6。

## 1. 起動を確かめる (push の直後に 1 回)

```bash
gh run list --branch <枝> --limit 4 --json name,event,status,headSha -q '.[] | "\(.name) \(.event) \(.status) \(.headSha[0:9])"'
```

`check`・`build` (`wt/sep-*` の枝は `sep` も) が、push した SHA で出ていること。出ていない時:

| 状態 | 理由 | 直し方 |
|---|---|---|
| 1 本も無い | コミットの本文に CI を飛ばす印 (角括弧つきの skip ci) の文字列がある。説明として書いても飛ぶ | 印を含まないコミットを足す (空のコミットでよい) |
| build だけ無い | 変更が文書だけ (build.yml の paths-ignore) | 正しい。文書だけは check の success で取り込む。build は手で起こさない (ユーザー決定 2026-11-19、PARALLEL_BRANCHES §3) |
| build だけ無い (文書以外も変えた) | 空のコミット・同じ木の force-push | `gh workflow run build.yml --ref <枝>` |
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

- コードを含む SHA は単独の段でも `wt/int-<日付><記号>` に push し、早送り前に `python3 -B tools/ci/land_gate.py <SHA>` の成功とその SHA の受入 PASS を確かめる。
- 待ちの終了と merge・push を 1 本のコマンドに繋げない。結論を読んでから取り込む。
- 所要の目安 (2026-11-18 の棚卸しの後): push から最後のジョブまで 30〜36 分。check は 17〜22 分 (static-check 5〜7 分・ppc-compile 4〜7 分・ppc-qemu の 4 本は 14〜22 分)、build の checks 6 本は 11〜36 分。新しい枝の最初の回で PowerPC のジョブが 15 分ほど長い時は、クロスコンパイラのキャッシュが main に無い (ログの `Cache not found`)。基準から 2 割以上伸びたらスキル os32-ci-inventory。
- 上限に当たったら `gh api rate_limit -q '.resources.core'` の `reset` (UNIX 秒) まで待つ。

## 3. 赤を読む

```bash
gh run view <run> --json jobs -q '.jobs[] | "\(.name) \(.conclusion) \(.startedAt[11:16])-\(.completedAt[11:16])"'
```

| ジョブ | 落ちた検査の探し方 |
|---|---|
| build の `checks (i)` | `jid=$(gh run view <run> --json jobs -q '.jobs[] \| select(.name=="checks (i)") \| .databaseId')`、`gh api repos/<owner>/<repo>/actions/jobs/$jid/logs > log`、`grep -n '\*\*\* \[.*check-' log` (検査の名前)、その行の 30 行前までが失敗の本文 |
| check の `static-check`・`ppc-compile` | 同じくジョブ単位でログを取り、`grep -n 'Traceback\|##\[error\]' log`。最初の失敗で止まるので、後ろの検査は回っていない |
| check の `ppc-qemu (i)` | ジョブの要約の表 (失敗・回っていない・遅い検査)。ログは `gh run download <run> -n ppc-qemu-failed-<i> -D <置き場>` → `results.json` と `failed-logs/<検査>.log`、ゲストの証拠は `failed-logs/evidence/<検査>/` |
| sep の `checks` (手動) | `gh run download <run> -n sep-checks-<i> -D <置き場>` → `results.json` の `status: fail` の行と `failed-logs/<検査>.log` |

- `gh run view --log-failed` が空を返す事がある。ジョブ単位の API (`actions/jobs/<id>/logs`) で取る。
- `gh run download` は置き場を `-D` で渡す (渡さないと `would result in path traversal` で取れない事がある)。
- apt は update を 300 秒、install を 600 秒で打ち切り、TERM の 30 秒後も残るプロセスには KILL を送る。static-check の summary で「用意の失敗」は apt の rc (124 は時間切れ)、「環境が足りない」は preflight が挙げた道具と入れ方を見る。後続の統計 `ccache -s` は `always()` で動くため、その rc=127 より先の準備失敗を読む。
- **ジョブが 1 分以内に落ちたら、検査は始まっていない** (用意の失敗)。ログの最後の `##[group]Run` の手順を見る。2026-11-18 は `rustup toolchain install` が `rust-src` の `detected conflict` で落ちた。回し直して通ったら台帳に残す (§5)。
- ジョブの所要が 1 分以内なら、手順が飛ばされていないか `--json jobs -q '.jobs[].steps[]'` で確かめる (範囲の判定で中身が回らない事がある)。変異が回ったかは、ログの `RED` の行の数で分かる。
- 変異込みで初めて見える赤の型: 変異の時だけ作る写しの木にディレクトリ・ヘッダが足りない、置き場の移動で走査の根 (`tools/source_roots.py`) から外れて違反を検出しなくなった、変異の試験だけが要る道具がジョブに無い、ソースを特定のコミットにバイトで固定している試験、同じシャードで並んで回る検査どうしの競合 (片方が作業木に残したファイルを、木を走査する検査が拾う)。

## 4. 手元で再現する

```bash
MUTATE=1 make check-<名>                      # 1 本を変異込みで
python3 -B tools/checks/run.py --select --files <触ったファイル…>   # 触ったファイルを読む検査の一覧
```

- 手元の作業木に `.env` が無ければ `CROSS_DIR=/home/hight/opt/cross` (PowerPC は `PPC_CROSS_DIR=/home/hight/opt/cross-ppc`) を付ける。
- 関係する検査を型の名前で選ぶと漏れる。`--select --files` の一覧から選ぶ (全部は回さない。全体は CI)。
- `manifests` などの鮮度の検査は、`make all` をし直してから。

## 5. 揺れる検査

回し直し (`gh run rerun <run> --failed`) で通したら、検査の名前・落ち方・日付を 持越しの台帳 `docs/tasks/DEFERRED_TESTS.md` に行で残す。同じ検査の 2 回目が出たら、直すまで行を閉じない。3 回目は、回し直さずに直す。

原因の調べ方 (2026-11-18 に `ppc-usb` の揺れを直した時の手順):

1. 落ちた回の失敗の文を、回ごとに並べる。毎回同じ判定で落ちているなら、負荷ではなく試験の条件を疑う。
2. その判定の直前の「待ち」が、何を待っているかを読む。「出力の末尾がプロンプト」のように、結果が出る前にも成り立つ条件になっていないか。ゲストの側で、その条件を早く成り立たせる動き (行の描き直しがプロンプトを出し直す、など) を製品のソースで確かめる。
3. 同じ試験の隣の場合に、同じ型の直しの前例が無いかを `git log -S` で探す。
4. 直しは「待つ条件を、確かめたい結果そのものにする」。期限を延ばす直しはしない (何を待っているかが分からないまま通るだけになる)。
5. 手元で `MUTATE=1 make check-<名>` を回し、変異の検出の数が前と同じ事を見る。
