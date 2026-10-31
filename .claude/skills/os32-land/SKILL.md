---
name: os32-land
description: レビューを通った段 (作業木のブランチ) を main に取り込み、専用の作業木で検査して rc=0 のときだけ push し、CI を確かめて作業木を片付ける。PM が段・後片付け・文書を main に入れるときに使う。検査の基点 (最後の全体検査) の更新、落ちたときの直し方、台帳の閉じ方も含む。実装の依頼は os32-delegate、設計の往復は os32-design-rally。
---

# os32-land — 段の取り込み・検査・push

規則の正典は [ROLES §0](../../../docs/tasks/agents/ROLES.md) (検査の段、全体検査は節目、検査中も main を編集してよい)。
ここはその手順を 1 本にしたもの。

## 1. 取り込む前

- レビューが「取り込み可」か。要修正なら直してから (軽い指摘は PM が直すか、次の段の依頼に書いて設計票の末尾に「送り項目」として残す)。
- 作業木に未コミットが無いか (`git -C ~/wt/os32-v3/<名> status --short`)。PM が直したら作業木でコミットしてから merge。
- 直しのログは**最後の変更より後**のものだけ合格に数える。PM が直したら、その直しに当たる試験を自分で回す
  (`CROSS_DIR=/home/hight/opt/cross ~/os32-tmp/bin/check_slot.sh <名札> bash -c "make <check-…> MUTATE=1"`)。

## 2. merge

```bash
cd /home/hight/os32-v3
git merge --no-ff -q wt/<名> -m "<段>: <中身> — <書き手> 実装、<レビュアー> レビュー取り込み可

Co-Authored-By: …
Claude-Session: …"
git worktree remove --force ~/wt/os32-v3/<名> && git branch -d wt/<名>
```

衝突したら止まって原因を見る (並列の段が同じファイルを触った)。`-X ours/theirs` で潰さない。

## 3. 検査 (専用の作業木、バックグラウンド)

| 何を入れたか | 検査 |
|---|---|
| 文書だけ (docs・票・CLAUDE.md・スキル) | その場で `CHECK_NO_NEEDS=1 make check-docs-links check-docs-orphans check-docs-status check-docs-tree check-constraints MUTATE=0` (`CHECK_NO_NEEDS=1` は宣言の前提 (`make all`) を組まない — 文書の検査は成果物を読まない。check-docs-tree は相互リンクの数を基準と比べる — リンクを足したら必ず入れる、2026-10-28 に ROLES↔POLICY_DEV の相互リンクで落ちた)。**検査が読む文書を触ったら、その検査も足す** — 例: `docs/KAPI_SPEC.md` の表 → `check-kapi-version` (表の行の形を読む。2026-10-25 に 481a8bf6 で行に説明を足して CI の check が落ちた)。どの検査が文書を読むかは `python3 tools/checks/run.py --select --files <文書>` で出す |
| 段・小さな取り込み | `CHECK_BASE=<最後の全体> ~/os32-tmp/bin/check_wt.sh <名札> <SHA>` (`make all` + `make -k check-changed`) |
| 節目 (票の終わり・実機配備の前・検査の仕組みを変えた・tier-2 の push の 1 日後) | `~/os32-tmp/bin/check_wt.sh <名札> <SHA>` (全体) |

- `check_wt.sh` は `run_in_background` で回し、通知を待つ (結果は `~/os32-tmp/run/<名札>/{rev.txt,all.log,check.log}`)。
- **check-changed が `→ full` と出たら** (変更が多い・Cargo.toml や build/*.mk に触った) その回は全体検査になる。
  rc=0 ならその SHA を**新しい最後の全体検査**にして、以後の `CHECK_BASE` を差し替え、HANDOVER §1 に書く。
- 検査中に main へ次の段を merge してよい。push するのは検査した SHA まで (`git push origin <SHA>:main`)。

## 4. 落ちたとき

1. `tail -1 rev.txt` で rc、**落ちた対象は `awk -F'\t' '$3!=0' check_times.tsv` で全部出す**。check.log の `^make…: ***` の grep は使わない — 並列の検査の出力が混ざって `make[3]: ***` が行頭に来ないことがあり、2026-11-01 に落ちた 2 本のうち 1 本を見落として push した (CI の check で発覚)。
2. その対象の周辺だけ読む (`sed -n` で行を絞る)。MUTATION の RED は期待どおりの行なので数えない。
3. 直して main に別コミット。**落ちた対象だけ**を回し直して rc=0 を確かめる。全体を回し直さない。
4. 失敗した作業木は `git worktree remove --force ~/wt/os32-v3/check-<名札>` (残すのは調べるため)。

今までに踏んだもの:
- 状態行が語彙の外 (`docs/POLICY_DEV.md` §8 の表: 計画 / 設計中 / 実装中 / 受入待ち / 受入完了・実機確認待ち / 受入完了 / 撤回 / 完了記録 / 草案 / 現行。「計画中」「実装完了」は語彙の外)。
- gshell を rustc で直に組む fixture が、build.rs の作る OUT_DIR のファイルを知らない (`test_gui_desc.py` の build_fixture に足す)。
- `CHECK` の `covers` と依存抽出に入力が無く、変更しても試験が選ばれない。
- 作業木の `assets/fonts` に IPAex が無い (同意済みの main から写す、gitignore)。
- CI だけ落ちる: CI は 32bit の試験を native で走らせる (手元は qemu)。`HOST32_RUNNERS="native qemu"` で再現する。
- CI だけ落ちる (2): `.github/workflows/check.yml` は検査を**名前で直接**呼ぶ。手元の check-changed はこのファイルを見ないので、検査を消す・改名・束ねた取り込みは `grep -rn <検査名> .github` で参照が残っていないか見てから push する (2026-10-24、TT5 で消した 2 本の名前が残り CI が `No rule to make target` で落ちた)。

## 5. push と CI

```bash
tail -1 ~/os32-tmp/run/<名札>/rev.txt     # rc=0 を見てから
git push -q origin <SHA>:main             # ; や && で検査とつながない (別のコマンド)
gh run list -L 2                          # build と check
```

- CI の build が赤なら最優先で直す (`gh api repos/ske-studio/os32-v3/actions/jobs/<id>/logs` で全文、`--log-failed` が空のときの代わり)。
- 文書だけのコミットには build が走らない (path filter)。実機に入れる SHA は build 成功のものを選ぶ (os32-realhw-update)。

## 6. 記録

- 票の状態行に段の着地と受入の証拠の所在。延ばした試験は [DEFERRED_TESTS](../../../docs/tasks/DEFERRED_TESTS.md) に 1 行。
- 台帳の行を閉じるとき: 証拠 (RESULT.md) を確かめ、閉鎖台帳 (アーカイブ: `docs/archive/v3/DEFERRED_CLOSED.md`) の末尾へ
  `| ID | 中身 | 日付 | 証拠の所在 — 何で閉じたか |` を足し、現行の台帳から行を消して状態行を更新。
- 節目では HANDOVER の §1 (最後の全体検査・KAPI の版) と §2 (次の一手) を書き換える。
