---
name: os32-land
description: レビューを通った段 (作業木のブランチ) を push して GitHub の CI (restructure-build) を通し、通ったら main に取り込んで作業木を片付ける。PM が段・後片付け・文書を main に入れるときに使う。検査の基点 (最後の全体検査) の更新、落ちたときの直し方、台帳の閉じ方も含む。実装の依頼は os32-delegate、設計の往復は os32-design-rally。
---

# os32-land — 段の取り込み・CI・push

規則の正典は [ROLES §0](../../../docs/tasks/agents/ROLES.md) (検査の段、全体検査は節目、検査中も main を編集してよい)。
ここはその手順を 1 本にしたもの。

> **作り直し票の取り込み**は `wt/int-*` の候補 SHA を push し、`restructure-build` の同じ run の最新 attempt の全ジョブと全件集計（変異込み）の成功を待つ。`python3 -B tools/ci/land_gate.py <SHA> --evidence <JSON>` に候補の sha・run_id・attempt・boots を渡す。boots は今回の PC-98 と PowerMac の媒体・sha256・artifact_id・到達点・ログ。文書だけでも両方を取り直す。再生成の一致は登録の build_identity が確かめる。古い検査は関門ではない (P7 で消す)。参考に読むだけ。[CI 設計 §4・§8](../../../docs/design/tools/ci/ci_v2.md)

## 1. 取り込む前

- レビューが「取り込み可」か。要修正なら直してから (軽い指摘は PM が直すか、次の段の依頼に書いて設計票の末尾に「送り項目」として残す)。
- 作業木に未コミットが無いか (`git -C ~/wt/os32-v3/<名> status --short`)。PM が直したら作業木でコミットしてから merge。
- 直しのログは**最後の変更より後**のものだけ合格に数える。PM が直したら、その直しに当たる試験を自分で回す
  (`python3 -B -m tests.infra.ci run --check <id>`、足した・触った検査は `--mutate` 付き)。

## 2. merge

```bash
cd /home/hight/os32-v3
git switch wt/int-<日付><記号>
git merge --no-ff -q wt/<名> -m "<段>: <中身> — <書き手> 実装、<レビュアー> レビュー取り込み可

Co-Authored-By: …
Claude-Session: …"
git worktree remove --force ~/wt/os32-v3/<名> && git branch -d wt/<名>
```

衝突したら止まって原因を見る (並列の段が同じファイルを触った)。`-X ours/theirs` で潰さない。

## 3. 検査

GitHub の CI (ユーザー決定 2026-11-05、メインストリームを含む)。

| 何を入れたか | 検査 |
|---|---|
| 文書だけ | push 前に対象の登録検査を ID で回す。取り込みは次の行と同じ。 |
| すべての取り込み | 候補 SHA を `wt/int-*` に push し、今回の両機種の起動記録を揃えて `python3 -B tools/ci/land_gate.py <SHA> --evidence <JSON>` の成功を確かめる。 |

- 待ちには上限を付ける。取り込み直しは PARALLEL_BRANCHES §3 の G と M の手順に従う。
- 並列の枝が先に main に入ったら、main を取り込んで push し直し、CI を通し直す。

## 3-1. ゲストの受入と実機は CI の成果物から

同じ run の最新 attempt の成果物を使い、機種ごとに媒体・sha256・artifact_id・到達点・ログを記録する。

## 4. 落ちたとき

1. 失敗したジョブのログと集計結果から検査 ID・構成・失敗理由を読む。
2. 候補の作業木で直し、`python3 -B -m tests.infra.ci run --check <id>` で確かめる。変異の失敗なら `--mutate` を付ける。
3. 直した候補を push し、§3 の証拠を取り直す。
4. CI でだけ落ちて手元で再現しないときは、substream (Ubuntu 24.04・gcc 13・qemu 8.2、GitHub の runner と同じ) で再現する (`ssh substream`、2026-11-05 の gcc 13 の件)。

今までに踏んだもの:
- 状態行が語彙の外 (`docs/POLICY_DEV.md` §8 の表: 計画 / 設計中 / 実装中 / 受入待ち / 受入完了・実機確認待ち / 受入完了 / 撤回 / 完了記録 / 草案 / 現行。「計画中」「実装完了」は語彙の外)。
- gshell を rustc で直に組む fixture が、build.rs の作る OUT_DIR のファイルを知らない (`test_gui_desc.py` の build_fixture に足す)。
- `CHECK` の `covers` と依存抽出に入力が無く、変更しても試験が選ばれない。
- 作業木の `assets/fonts` に IPAex が無い (同意済みの main から写す、gitignore)。
- CI だけ落ちる: CI は 32bit の試験を native で走らせる (手元は qemu)。`HOST32_RUNNERS="native qemu"` で再現する。
- 自分が書いていない変更がコミットに紛れる: 作業木はほかのエージェントも書ける。`git add -A` の前に `git status --short` を読み、覚えの無いファイルは外して出所を確かめる (2026-11-17、`userland/diag/hw/tri_burst.hwb` の 18 行が紛れて検査が落ちた)。
- CI が起動しないときは、候補の枝名と push した SHA を確かめる。
- CI の待ち: `gh run watch` を何本も裏で回さない (3 秒ごとに問い合わせて、API の上限 1 時間 5,000 回を使い切る)。`gh run list` を数分おきに 1 回。
- 揺れる試験: 回し直し (`gh run rerun <id> --failed`) で通したら、検査の名前・落ち方・日付を `docs/tasks/DEFERRED_TESTS.md` に行で残す。同じ検査の 2 回目が出たら、直すまで行を閉じない (回し直しで通す運用を常態にしない)。

## 5. main への取り込みと CI

```bash
git switch main
python3 -B tools/ci/land_gate.py <SHA> --evidence <JSON> &&
git merge --ff-only <SHA>
git push -q origin main
```

- 取り消された attempt は成功に数えない。
- main の CI の build が赤なら最優先で直す。
- 実機に入れる SHA は artifacts のジョブが成功したものを選ぶ (os32-realhw-update)。

## 6. 記録

- 票の状態行に段の着地と受入の証拠の所在。延ばした試験は `docs/tasks/DEFERRED_TESTS.md` に 1 行。
- 台帳の行を閉じるとき: 証拠 (RESULT.md) を確かめ、閉鎖台帳 (アーカイブ: `docs/archive/v3/DEFERRED_CLOSED.md`) の末尾へ
  `| ID | 中身 | 日付 | 証拠の所在 — 何で閉じたか |` を足し、現行の台帳から行を消して状態行を更新。
- 節目では HANDOVER の §1 (KAPI の版など) と §2 (次の一手) を書き換える。
