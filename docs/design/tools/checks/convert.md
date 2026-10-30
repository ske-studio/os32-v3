# tools/checks/convert.py — 一時変換と3集合の同値比較

> 状態: **現行 (2026-10-31)** — 出所: TASK_POLICY_LIGHT の R0 (astra 起草、Opus 2 往復で確定)。実装は R1 から。

## 役割と境目

mk の recipe、yaml の入力、旧選択器の共通方針を CHECK と run へ移す一時道具。
共通の [Check・Plan](README.md) を境界にし、検査の期待値・正常対照・変異・合否は変えない。

```python
from collections.abc import Callable, Mapping
from pathlib import Path

def convert(root: Path, output: Path, tiers: Mapping[str, int],
            timings: Mapping[str, float]) -> list[Path]: ...
def compare_commits(root: Path, frozen_old: Path, commits: tuple[str, ...],
                    new_plan: Callable[[tuple[str, ...]], "Plan"]) -> int: ...
def main(argv: list[str]) -> int: ...
```

convert は README の Check に正規化できる宣言を出し、比較は注入された new_plan の3集合を読む。
R1(b) はこの契約で実装し、R1(a) の内部 deps・選択実装を編集しない。別の型ファイルや旧方針投影は作らない。
状態は作業用の変換結果、変換前 snapshot、tier/計時入力、SHA列と比較結果だけ。恒久登録表は作らず、R3 で道具を削除する。

## 変換の順序

1. 旧 `read_makefiles/check_lists/load_map` を一時利用する (`tools/check_select.py:92,200,235`)。通常237本と列外目標を区別する。
2. 1スクリプトに一意の1検査なら先頭へ CHECK、末尾へ run を追加する。既存 __main__ は保つ。
3. 複数スクリプト・道具・unittest・gcc recipe は `tools/checks/<name>.py` に薄い run を作る。
   共有スクリプトの入口は検査ごとに分ける。例: `test_about_info.py` は ver-info/tools-host の両方 (`build/checks.d/check-ver-info-host.mk:8`、`check-tools-host.mk:11`)。
4. MUT/MUTS は ctx.mutate、host32_check は ctx.runners に従う。旧コマンドの行順・環境・引用符・失敗中断を保持する。
   既存 __main__ を `python3 -B` で呼べば unittest や引数なし main も保てる。宣言に runners は作らない。
5. make 前提を `make:` needs、検査前提を `check:` needs にする。ver-info の11検査は選択閉包に足すだけ。
   np21w-gui のフォント、sqlite-module-proto を保持する。arm/gfinit/e10c/local4本は手書き make に残す。
   e10c-lifecycle は既存の memmap-audit-host への別名を保ち、成功だけを返す CHECK は作らない。
6. yaml の入力を covers にする。自己検査ファイルは自動入力。旧 glob は現在のファイル一覧へ固定展開せず、新規入力にも当たる形を保つ。
   ヘッダは run の依存抽出で同じ当たりを復元できるものだけ除く。直接読むヘッダ・.inc・共有Python・データは復元不能なら残す。
   自己 mk/yaml は比較中の snapshot に保持し、削除後の実在 lint へ持ち越さない。R3 で削除する covers 参照を同時に外す。
7. 成果物 covers と既存の精密な make 前提を保つ。従来外側で all を組んでいた読み手は `make:all` とする。
8. tier は初期1、docs_always は0、PM の共有一覧は3。安定11候補の tier4化・変異4化・製品検査削除は P3。
   budget は `n8/check_times.tsv` の正常系秒×2、未測定60。mutate_tier は省略可で既定2。

未知の recipe は対象名・行を示して未変換で止める。空の run や成功を返す代用品で埋めない。
個別の既存 recipe から薄い run を生成し、汎用 Make 解釈器を作らない。同じ入力で同じ出力を作業用の木へ出し、差分を確認できる形にする。

## 200コミットの同値 (U1 決定)

対象は R2 開始時 main の先頭から `git rev-list --no-merges -n 200 main`。main SHA と200件の SHA列を結果に保存する。
各 commit と第一親の差分を `git diff-tree --no-commit-id --name-only -r --no-renames <parent> <commit>` で得る。改名は旧名・新名を含む。
比較は同じ現在の入力木・登録に過去の変更一覧を入れる。各歴史時点の検査数を混ぜず、旧側の mk/yaml/selector は変換前 snapshot に固定する。
旧自動探索が新入口を拾わないよう旧と新の作業木を分ける。--files 相当なので base=None とし、歴史の Makefile に対する make_narrow は実行しない。

旧 plan() の mode 分岐直前 (`tools/check_select.py:1378`) に観測点を足し、以下の3集合を出す。分岐・戻り値の意味は変更しない。

| 比較する集合 | 旧側 | 新側 |
|---|---|---|
| hit | 変更に当たった検査名。check- を外して正規化 | Plan.hit |
| unmatched | 具体的登録がなく、docs/notest/ignore の対象でもない変更パス | Plan.unmatched |
| full_hits | 旧 full/build入力判定で全体検査へ送る変更パス | Plan.full_hits |

200件すべてで3集合がそれぞれ一致することを確認する。旧 CC_STAGE1/CC_MUT1 を当たりの代用にせず、旧方針の投影や互換モードは実装しない。
新の比較値は新 CHECK/covers/deps から得る実際の Plan とし、旧 yaml の結果をコピーして一致させない。
旧自己 mk/yaml を含む差分も比較 snapshot で扱い、対象コミットやヘッダ変更を除外しない。
差は commit・変更入力・集合名・旧のみ/新のみ・原因を記録する。原因の説明だけで一致にせず、covers または依存抽出を直して再比較する。**例外は 1 種だけ**: 旧で broad の検査の `**` だけが当たり unmatched に残った変更が、新で解消されること (`**` も解消に数える PM 決定)。旧 unmatched から、旧 broad の `**` に当たるパスを除いた集合と、新 unmatched が一致すればよい。
fast/docs/full/段の方針差は [run の試験節](run.md) の事例で確認する。新の段4昇格を比較用 full_hits に混ぜない。
200件は入力移植の同値を確かめるもの。recipe の実行は下記 R1/R3 の試験で見る。

## R1〜R3 の作業 (ファイル単位)

| 段・担当 | ファイル | 作業・依存 |
|---|---|---|
| R1(a) sol | tools/checks/run.py、test_run.py | README の型、CLI、選択用deps、4段、make受渡しと run の試験節。2ファイルを同じ担当が所有 |
| R1(b) sol | tools/checks/convert.py | 変換、旧plan観測点、3集合同値。README の型を境に (a) と並列可 |
| R2 PM | tools/tests/test_*.py、tools/checks/<name>.py | 変換実行、tier確定、既存計時からbudget。共有スクリプトの所有を決めて入口単位に分ける |
| R2 PM | 作業領域の snapshot・比較結果 | main/200 SHA/環境を固定して3集合比較。R1(a)(b) の結合後 |
| R3 terra | build/sdk.mk | --list、静的パターン、CC値、親makeでCC_NEEDSを1回準備。check-changed は下限2。列外7入口を手書きで残す |
| R3 terra | tools/gen_tests_inventory.py | --list --json の CHECK を読む。CI解析・記録抽出は維持し、旧importとmk登録照合を削除 (`:49,523`) |
| R3 terra | tools/test_changes.py | TEST_PREFIXES の旧2ディレクトリを tools/checks/ へ置換 (`:11`) |
| R3 terra | tools/tests/test_dep_dir_d_console.py:202 | yaml の GRID_FILES 照合を、その検査の `CHECK["covers"]` との照合に置換 |
| R3 terra | tools/tests/test_dep_dir_d_display.py:97 | yaml の FILES 照合を、その検査の `CHECK["covers"]` との照合に置換 |
| R3 terra | tools/tests/test_dep_dir_d_splash.py:83 | yaml の FILES 照合を、その検査の `CHECK["covers"]` との照合に置換 |
| R3 terra | tools/tests/test_ci_stab.py:91,129 | checks.d glob を CHECK の run 入口からのスクリプト収集へ。既存 test_*.py の列と正常対照監査を保持し、方言の例外文字列も更新 |
| R3 terra | tools/tests/test_c_dialect.py:716 | dialect_variants_needed の import を run へ。P1b と同じファイルの変更は直列にする |
| R3 terra | tools/tests/test_check_times.py:36 | 新入口へ計時fixtureを変更し、既存の環境分離・rc確認を保つ |
| R3 terra | tools/tests/test_harness_tools.py:30,168 | pack_case と check_select の CASES/MUTATIONS を削除。他の test_changes/docs-status の試験は保持 |
| R3 terra | .claude/skills/os32-delegate/SKILL.md、os32-land/SKILL.md、.agents/skills/os32-delegate/SKILL.md | 旧選択器呼出しを run へ替え、欠けた --select を補う。pack/inputs/suggest の代替入口は作らない |
| R3 terra | docs/08_build.md、docs/tasks/agents/ROLES.md | 新操作と選択の説明へ切替。P2の全規約整理は分ける |
| R3 terra | .github/workflows/check.yml、build.yml | 呼出し名は維持し、旧形式を指すコメントのみ必要なら修正 |
| R3 terra | build/checks.d/、tools/check_map.d/、tools/check_map.yaml、tools/check_select.py | 切替コミットで即削除。旧 selector 試験2本は必要分を test_run へ移した後に削除 |
| R3 terra | tools/checks/convert.py | R2 の記録を作業領域に残して削除。pack.py は作らず、旧 --pack とともに消す |

R1 の並列は実装の分担であり、R0 でエージェントを起動する指示ではない。P1b の checker と R1 は別ファイル。
R3 は R2 の一致と P1b の引数契約を前提とし、参照置換・切替・旧形式削除を同じまとまりで行う。配置移動は後の R4/P4。
console/display/splash は従来照合した GRID_FILES/FILES を covers に保持して照合し、依存抽出だけへの置換で照合を空にしない。
ci_stab は CHECK のあるファイルと run が呼ぶ既存スクリプトを静的に集める。検査を実行して列挙せず、新しい宣言項目も足さない。

## 失敗と試験

変換未対応・3集合の不一致は対象と原因を示して非0。旧コード削除を先行しない。
R1 の変換確認は単独script、host32、複数行、unittest、make変数、check前提、列外/別名の既存 recipe 各型で行う。
R1(a) はランナー試験を所有し、R1(b) は convert の動作を確認する。結合時に実際の Plan を渡して比較の受渡しを確認する。
R3 は直接 make 名、CI の28入口、check-fast/full の列、差分実行、準備失敗と検査rcを確認する。長い実検査は PM が既存制限の下で1回行う。
段4の列は旧通常237本を基準に移行による置換を名で対応させ、列外目標を混ぜない。
直近10件の取り込み差分で build・正常・変異・静的を含む時間を測る。段3のゲスト時間は別記し、10分未達は測定事実として示す。
新しい時間上限や未承認の製品検査削除を受入へ足さない。
