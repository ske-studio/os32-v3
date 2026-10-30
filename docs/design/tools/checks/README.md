# tools/checks — 宣言と選択結果の契約

> 状態: **現行 (2026-10-31)** — 出所: TASK_POLICY_LIGHT の R0 (astra 起草、Opus 2 往復で確定)。実装は R1 から。

目的は、検査の選択情報を検査自身へ集め、取り込み時の待ち時間を減らすこと。
根拠は `/home/hight/os32-v3` の `docs/tasks/v3/TASK_POLICY_LIGHT.md` §C・C-2・F・G と今回の BRIEF。
本稿では BRIEF の修正を優先する。既存検査の合否・正常対照・変異は保ち、新しい検査や受入条件を足さない。
設計はこの README、[run](run.md)、[convert](convert.md) の3本。初稿 6 本はリポジトリの外 (~/os32-tmp/run/check-runner/design.r1/) に残した。

## 宣言と型 (R1 の共通境界)

```python
# shebang・符号化・docstring・必要な __future__ の直後、通常 import より前
CHECK = dict(name="dma-pool-host", tier=1,
    covers=["mm/dma_pool.c", "mm/dma_pool_math.c"],  # 型の例。実際の全入力ではない
    needs=[], budget=60)  # mutate_tier は省略可

def run(ctx: "Context") -> int: ...
```

| 項目 | 型・既定 | 意味 |
|---|---|---|
| name | str、必須 | 既存の make 名から `check-` を外した名 |
| tier | int、既定1 | 0は文書共有、1/2は当たりで実行、3は段3以上なら当たらなくても実行、4は節目のみ |
| covers | list[str]、必須 | 組む・読むソース、スクリプト、データと必要な glob。自己ファイルは自動入力 |
| needs | list[str]、既定[] | `make:sqlite-module-proto` は組立前提、`check:ver-version-host` は選択へ加える検査 |
| budget | float、必須 | 正常系の目安秒。記録値の2倍、未測定60。並びと超過報告に用い、合否は変えない |
| mutate_tier | int、既定2、任意 | 当たりの変異を始める段。R1 の lint は記載を要求しない |

AST で literal な `dict(...)` を読み、タプルと既定値へ正規化する。実装上の型は `run.py` が所有する。

```python
from dataclasses import dataclass
from pathlib import Path

@dataclass(frozen=True)
class Check:
    name: str
    path: str                    # CHECK を持つリポジトリ相対パス、宣言項目ではない
    covers: tuple[str, ...]
    budget: float
    tier: int = 1
    needs: tuple[str, ...] = ()
    mutate_tier: int = 2

@dataclass(frozen=True)
class Plan:
    tier: int
    mode: str                    # fast / docs / sel / full
    hit: frozenset[str]           # 段の方針適用前の当たり、check- なし
    unmatched: frozenset[str]     # 具体的な登録の無い変更パス
    full_hits: frozenset[str]     # 旧 full 入力判定に当たった変更パス
    stage: tuple[str, ...]        # 実行名。check 前提の閉包を含み、重複なし
    mutations: tuple[str, ...]    # stage の部分集合
    needs: tuple[str, ...]        # stage の make 前提の和。make: は外す
    files: tuple[str, ...]        # 変更ファイルそのもの
```

`hit/unmatched/full_hits` は mode・段・変異を決める前の集合として、空でも常に返す。
`full_hits` に段の推定理由を混ぜない。KAPI 等の段4への昇格は後段の方針で行う (run の規則4)。
`Check.name` と Plan 内の検査名には接頭辞を持たせず、CLI の make 名出力で `check-` を付ける。
`scope`・`runners`・`broad`・`local_only` は宣言に持たない。`**` を含む glob の当たりも unmatched の解消に数える (PM 決定 2026-10-31、軽い方へ。旧は broad の 37 本の `**` だけを数えなかった)。
HOST32_RUNNERS とその既定は既存 make が所有する。差分を受けるかは各 `run(ctx)` が既存 checker の引数契約に従う。
arm-compile・gfinit-fixture-host・e10c-lifecycle・local4本は手書きの make 目標として残し、通常列の CHECK にしない。
ヘッダは依存から同じ当たりを復元できるものだけ省く。直接読むヘッダや復元不能な入力は covers に残す。

## 置き場・状態・失敗

登録元は CHECK だけ。列挙のために検査を import しない。宣言と Plan は1呼出しの値で、恒久の登録表や依存キャッシュを増やさない。
R1/R2 は `tools/tests/test_*.py` に宣言を足し、集約入口・道具ラッパーを `tools/checks/` に置く。
共有スクリプトに CHECK を二つ置かず、検査ごとの入口を分ける。ソースに沿う配置への移動は同値確認後の R4/P4。
構文・型・名前重複・既存の入力監査の失敗は非0。budget 超過は表示だけ。製品検査の終了コードは維持する。

## 実物の差と今回の扱い

参照は `/home/hight/os32-v3` 内の相対 `file:line`。初稿の実測は通常237、mk244、yaml241。

| ID | 実物と扱い |
|---|---|
| D1 | `tools/check_select.py:1400` は正常系が全列。U1 は決定済み: 200件で比較するのは hit・unmatched・full_hits の3集合。旧方針投影は作らない |
| D2 | `tools/check_map.yaml:47` の docs_always は10本。文書段は tier4 を除く当たりとこの集合を回す |
| D3 | `tools/check_map.yaml:19` の broad は静的5本に限らない。属性を移さず、`**` を unmatched の解消に数えない共通規則へ |
| D4 | `tools/check_select.py:759,975` は静的 include 閉包と gcc 結果の和。この選択用依存を run に含める |
| D5 | `build/checks.d/check-arm-compile.mk:11`、`check-gfinit-fixture-host.mk:4`、`check-memmap-audit-host.mk:8` の列外入口は手書き make へ残す |
| D6 | `tools/check_privileged.py:132` は .o、`tools/tests/test_kprintf_window.py:310` は unittest と scanner。差分化は P1b の3本に限る |
| D7 | 旧形式参照は inventory・方言・計時・依頼パック試験に加え、`tools/tests/test_dep_dir_d_console.py:202`・`test_dep_dir_d_display.py:97`・`test_dep_dir_d_splash.py:83` の yaml 読取と `test_ci_stab.py:91` の checks.d glob。R3 で同時に置換する |
| D8 | `build/sdk.mk:174` の共有前提は親 make が1回組む。`build/checks.d/check-ver-info-host.mk:4` の11検査は選択の閉包へ加えるだけ |
| D9 | `.claude/skills/os32-delegate/SKILL.md:30`・`.agents/skills/os32-delegate/SKILL.md:29` は旧呼出しに --select が無い。置換時に補う |

未決は0件。R1 はこの Check・Plan 契約で (a) run.py と試験、(b) convert.py を並列に実装できる。
受入は既存の R1 道具試験、R2 の200件同値、R3 の直近10件計時・段4の列照合。詳細は各文書の試験節に置く。

## 既存の切り出し試験の直し方

1. 変更前の正常対照・振る舞い・変異の RED 数を記録する。
2. 関数本文の文字切り出しを消し、製品 `.c` 全体を試験 C から `#include` して組む。
3. 外部関数の小さな stub は `tools/tests/<名前>_host.c` に置き、製品内の関数はそのまま通す。
4. CPU 境界は `tools/tests/host_arch/` を使い、不要な節は `-ffunction-sections -fdata-sections -Wl,--gc-sections` で落とす。
5. 変異は一時ディレクトリの `.c` / `.inc` 全文コピーに当て、include の探索順でそのコピーを確実に使う。
6. 既存の入力・観測・正常対照を維持し、全体の前提で届かない境界値は実 helper を直接呼ぶ。
7. `test_exec_image.py` と `exec_image_host.c` を手本に、変更後も同じ数の変異が実行時に RED になることを確認する。
