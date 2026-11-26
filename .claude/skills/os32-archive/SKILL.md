---
name: os32-archive
description: 受入完了した票・役目を終えた設計を、今も効いている中身を現行の設計書 (docs/design) に要約してから docs/archive へ移す。票を完了にする時、現行の棚の文書を減らす時、試験の覚えや検査の出所がアーカイブを指している時に使う。要約の分担の依頼文、移動の道具、文書の検査、基準の書き直しまで。
---

# os32-archive — 要約してからアーカイブへ移す

決まりの正典は [POLICY_ARCHIVE](../../../docs/POLICY_ARCHIVE.md) と規則 [D4] ([CONSTRAINTS](../../../docs/CONSTRAINTS.md))。ここは手順。

## 1. 引いている所を挙げる

```bash
T=TASK_X                      # 票の名前 (拡張子なし)
git grep -n "$T" -- tests/registry.toml                          # 検査の登録の spec
git grep -ln "$T" -- 'tools/tests/*_tdd.md'               # 試験の覚え
git grep -n "$T" -- docs .claude ':!docs/archive'          # 仕様・索引・ほかの票・スキル
git grep -n "$T" -- docs/tasks/DEFERRED_TESTS.md          # 持越しの台帳
```

- 1 件も引かれていなければ §3 へ。
- 引かれていたら、どの中身が今の約束として使われているかを見る (試験の期待の出所・実装の約束・受入の手順)。

## 2. 要約して引き先を替える

要約は領域ごとにサブエージェントへ出す (1 領域 = 票 5〜10 本・覚え 10 本前後)。同じ作業木で並行に出してよい (担当のファイルが重ならないように領域を切る)。依頼に必ず書く事:

```
作業木 <パス> だけで作業。git の commit・push はしない (`git add -N <新しいファイル>` だけ可)。重いビルドはしない。
目的: 試験が確かめている契約を、今のソースと照らして docs/design/ に要約し、覚えの引き先を設計書に替える。
読んでよいアーカイブの文書: <担当の票の一覧> だけ (ユーザーの指示で読む)。アーカイブは編集しない。
手順: (1) 覚えと試験 (test_<名>.py・<名>_host.c・tests/<所属>/<id>.py) から契約を特定 (2) ソースで今成り立つか確かめる。
  票・覚えと今のコードが違えば、今のコードと試験を正とし、違いを報告 (3) docs/design/<ソースと同じパス>.md に書く
  (既に在れば節を足す。仕様に同じ契約が在ればその節を引き先に) (4) 覚えの「票:」「契約の正典は」を設計書のパスと節に替える。
書き方: 行番号でソースを指さない。設計書にアーカイブのパスを書かない。設計書どうしの相互リンクを作らない。
  無効な記述は削除。確かめていない事は書かない。冒頭は「# <パス> — 設計」「> 状態: **現行 (日付)** — 出所: …」「ソース: `…`」。
触らない物: ソース・試験のコードとその註、tools/ops/archive_refs_baseline.json、docs/INDEX.md、docs/archive/、
  docs/design/**/README.md、担当の外の覚え。
最後に: git add -N の後、python3 -B tools/ops/check_doc_line_refs.py と tools/ops/check_docs_links.py。
報告 (30 行以内): 書いた設計書 / 付け替えた覚えと残した参照 / 票・覚えと今のソースの食い違い (不具合の疑いは別に) / 確かめられなかった事。
```

報告を受けたら PM がする事:

- 「不具合の疑い」と「覚えを頼りに書いた」と申告された所を、ソースで確かめる。直しはコードの変更なので別のコミットにする。
- 新しい設計書を索引に足す: 置き場に `README.md` があればそこへ `- [<名>](<名>.md)`、無ければ `docs/design/README.md` の「その他の設計書」へ。
- 検査の登録の spec を設計書の節に替える (`tests/registry.toml`)。

## 3. 移す

```bash
python3 tools/move_docs.py --into docs/archive/<籠> docs/tasks/<領域>/TASK_X.md --dry-run   # 書き換えを見る
python3 tools/move_docs.py --into docs/archive/<籠> docs/tasks/<領域>/TASK_X.md
python3 -B tools/doc_delink_archive.py --dry-run     # 現行 → アーカイブのリンクを見る
python3 -B tools/doc_delink_archive.py               # 文字に戻す
```

- 籠は領域 + 版 (`docs/archive/README.md` の表)。表と、`docs/INDEX.md` の行 (所在を文字で `(アーカイブ: \`docs/archive/…\`)`) は手で直す。
- ソースの註は `--ext` で書き換えない (契約のヘッダは註も ABI の世代のハッシュに入る)。

## 4. 検査

```bash
git add -A -N .
python3 -B tools/ops/check_archive_refs.py --write-baseline     # 来歴の註・覚えの件数が変わった時
for id in docs_links docs_navigation docs_source_refs docs_status; do
    python3 -B -m tests.infra.ci run --check "$id" || exit 1
done
python3 -B -m tests.infra.ci lint
```

落ち方と直し方:

| 落ちた検査 | よくある理由 | 直し方 |
|---|---|---|
| `docs-orphans` | 新しい設計書が索引に無い | §2 の索引の行 |
| `docs-tree` の相互リンク・決定 → 票 | 設計書どうしが互いにリンクした | 片方を文字の参照 (バッククォートのパス) に。基準値は増やさない |
| `docs-tree` の現行 → アーカイブ | 移した票へのリンクが残った | `doc_delink_archive.py` |
| 登録の spec | spec が移した票の名前のまま | 設計書のパスに替える |
| `docs-status` | 完了した票が現行の棚に残った・進行中の票に完了の基準が無い | 移すか、`tools/docs_status_exceptions.json` に理由 |

取り込みはスキル os32-land (文書だけの変更でも、`tools/` の基準を触ったら build が要る)。
