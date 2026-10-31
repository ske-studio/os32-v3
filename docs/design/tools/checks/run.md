# tools/checks/run.py — 列挙・選択・実行と依存抽出

> 状態: **現行 (2026-10-31)** — 出所: TASK_POLICY_LIGHT の R0 (astra 起草、Opus 2 往復で確定)。実装は R1 から。

## 役割と境目

CHECK を唯一の登録元として CLI と make をつなぐ。選択用 deps はこのファイル内に含める。

```python
@dataclass(frozen=True)
class Context:
    root: Path
    mutate: bool
    runners: tuple[str, ...]     # 宣言でなく既存 HOST32_RUNNERS から
    files: tuple[str, ...] | None  # None=全範囲、()=変更なし
    tmp: Path
    make_vars: Mapping[str, str]

def discover(root: Path) -> dict[str, Check]: ...
def plan(root: Path, checks: Mapping[str, Check], files: tuple[str, ...],
         *, base: str | None = None, tier_floor: int = 0) -> Plan: ...
def inputs(root: Path, check: Check) -> frozenset[str]: ...
def compiler_headers(root, sources, inc_dirs) -> frozenset[str]: ...
def emit_shell(plan: Plan) -> str: ...
def run_one(check: Check, ctx: Context) -> int: ...
def dialect_variants_needed(files=None) -> bool: ...
def main(argv: list[str]) -> int: ...
```

## CLI

- `--list`: 通常列を `check-<name>` で1行1本。budget 降順、同値は name 順。
- `--list --json`: 同じ列の name/target/path と CHECK の値。inventory が読む。
- `--select [--base REF | --files PATH...] [--tier N]`: stdout は shell 代入、説明は stderr。明示空 --files と未指定を区別する。
- `--run NAME... [--files PATH...]`: name/check-name を受け、明示された検査と check 前提の閉包を実行する。単独 CLI の make 前提はまとめて1回組む。
- `--lint`: 宣言の型・名前・入力・既存の成果物読取監査。検査は実行せず、mutate_tier の省略を許す。

`--pack`・`--inputs`・`--suggest` は移さない。`--list --make`・make 断片・`--prepared` は作らない。

## 選び方 (8規則)

1. **列**: `tools/checks/**/*.py` と移動前の `tools/tests/test_*.py` の CHECK を AST で読む。宣言なしの部品・手書きの列外 make 目標は登録しない。
2. **変更**: --files があればその一覧だけ。通常は基点…HEAD、作業差分、非 ignore の未追跡の和。rename は両名。
   基準枝の選択は旧 `tools/check_select.py:1271,1279` を移す。ignore → full 判定 → notest/docs 分類、notest の except を保つ。
3. **当たりと3集合**: covers・自己ファイル・選択用ヘッダ依存に当たる検査名を hit にする。旧 glob の意味を保つ (`tools/check_select.py:57`)。
   unmatched は docs/notest/ignore 以外で、どの検査の covers にも当たらない変更 (`**` を含む glob の当たりも解消に数える)。
   KAPI 入力から生成物の読み手へ当たりを足す旧処理を保つ (`tools/check_select.py:1332`)。
   full_hits は旧 full/BUILD_CONFIG_GLOBS と build 入力の判定で得る変更パスの集合。旧 `:1378` の mode 分岐直前に対応する。
   3集合は段の昇格・共有追加・変異選択より前に確定する。notest に分類しても実際の hit は消さない。
4. **段**: full_hits、Makefile/build定義、KAPI入力・生成世代・link定義、ツールチェーン設定、ランナー自身は段4を最優先とする。
   旧 BUILD_CONFIG_GLOBS/KAPI_GENERATED を用いる。通常の CHECK ファイル変更は自己当たりとして扱う。
   `--base` があれば full glob に当たる各ビルド定義の基点と作業木を比較し、コメント・空行だけの差では段4へ上げない。当たりの計算には残す。
   make の recipe・define・継続行・エスケープされた `#` は保持し、曖昧な構文は実変更として扱う。基点なしでは従来どおり段4。
   変更なし/ignoreのみは従来の fast (段4、全列、変異なし)。notestだけで hit が空なら何も回さず、段0・mode=sel・空の stage/mutations/needs。
   docsのみは段0。残りは単一の非ヘッダかつ親が2成分以上で共有層以外なら段1、同じ深さ2以上の機能内なら段2。
   複数機能、親が1成分以下、include/kapi/mm/exec/lib/platform の層、または unmatched があれば段3。
   --tier はコード変更の推定段の下限。`make check-changed` は `--tier 2` を渡し、単一leafでも取り込みは段2以上。
   段1は実装者が直接 `--select` を使う場合だけ。docs/notestの特別分岐は既定の下限2で引き上げない。節目は make check。
5. **実行集合**: 段0〜3では tier4 を除く hit をすべて回す。当たった tier2/3 を浅い段という理由で削らない。
   これに tier0 を加え、段3以上では当たらない tier3 も加える。unmatched がある場合は通常列の tier≤3 を回す。段4は通常列全て。
   段0の文書集合は旧 docs_always を tier0 の初期値として保持する。notestだけで空の分岐では共有・静的も追加しない。
   コードの段1は既存の静的3本 (p2v/c-dialect/arch-asm) と check-map、段2/3はさらに privileged/kprintf-window を加える。
   check needs の閉包を加えて重複を除く。ver-info の前提もここで加えるだけで、実行依存グラフは作らない。
   変異は段1なし、段2/3は hit ∩ stage のうち mutate_tier≤段、段4は全て。docs の MUT1=stage と fast の MUT1=空は旧互換。
6. **準備**: stage の make needs の和が CC_NEEDS。親 make が検査の前に1回組む。make の内部前提は既存の build 規則に任せる。
7. **出力**: CC_MODE/CC_STAGE1/CC_MUT1/CC_SUMMARY の名前と quoting を維持 (`tools/check_select.py:1424`)。CC_TIER/CC_NEEDS/CC_FILES_JSON を足す。
   STAGE1/MUT1 は check- つき、MUT1⊆STAGE1。CC_FILES_JSON は元の変更パス。--select は build・検査・ゲストを起動しない。
   mode は fast/docs/sel/full を維持し、unmatched による段3は sel。notestだけの空列も sel と理由を表示する。
8. **lint**: 宣言の契約と既存の入力監査だけ。通常のパス/glob は追跡＋未追跡入力で照合し、生成物は未生成を許す。
   mk/yaml 相互照合は削除。成果物 covers と make needs、既存の読取監査・例外は保ち、build/out だけへ対象を狭めない。

## deps と静的検査の境界

CHECK 自身、covers と run が呼ぶスクリプトから旧 Extractor の静的 import/文字列/include 閉包を導く (`tools/check_select.py:719,759,819`)。
C/ハーネスを集めてから include を辿り、旧 `gcc -MM -MG -I.` と導出した -I の結果を合わせる (`:800,954,975`)。
選択に自動追加するのは抽出した入力と `-MM` に出るリポジトリ内のファイル (`.h`・`.inc`・その他の C 断片を含む)。復元できない直接入力は covers に残す。
文書以外の変更では compiler scan を使う。ソース単位の失敗は旧静的閉包へ戻し、gcc 起動失敗は非0。
キャッシュは同一プロセス内の lru のみ。root・入力条件を分け、ディスクへ保存しない。--list では依存抽出をせず、--lint はヘッダ全件コンパイルをしない。静的抽出で導く `.h`・`.inc` は covers の重複を要求しない。
成果物読取監査は旧 `:1015,1150` と PATH_AUDIT_ALLOW を移す。実行追跡や Python の汎用解釈器を作らない。
ランナーは変更ファイルをそのまま ctx.files へ渡す。翻訳単位の逆引きは P1b の各 checker 側が持つ。
p2v/c-dialect/arch-asm の run だけが既存 --files 契約へ渡す。未着地なら旧全走査。privileged/kprintf-window は旧入口を維持する。
段4・check-fast・変更指定なしの直接実行は files=None。空タプルは全走査へ変えない。差分なし時の正常対照は各 checker の既存契約に従う。

## make と実行

MUTATE=0/1/sel、MUTATE_TARGETS、MUT/MUTS は `build/sdk.mk:114` と同じ。単独 CLI の既定も1。
HOST32_RUNNERS の全列で正常対照、変異は先頭 runner だけ (`build/sdk.mk:156`)。host32 以外の処理は繰り返さない。
旧 recipe の行順、環境、失敗中断を run に移す。旧 __main__/unittest は subprocess の argv で呼び、一律 main() 呼出しへ変えない。
make は --list の名前に `.PHONY` と静的パターン `$(CHECK_NAMES): check-%:` を付ける。列外7入口は手書き make のまま。
check-changed は CC_NEEDS を1回組み、tree save → check-par → verify の順。check/fast は全列の needs を同様に集める。
親 make の準備対象は選択結果、全列時は --list --json の needs から得る。make 断片や check-time 間の依存辺は生成しない。
check-par の既存計時子は CHECK_TIME_CHILD=1 を渡す。子の --run は指定名の本体だけを実行し、閉包の再展開・前提の再準備をしない (`build/sdk.mk:169,174`)。
直接 make check-X/CLI --run は同じ閉包の make needs をまとめて1回組んでから実行する。ver-info の前提は重複なく選択へ加える。
BUILD_OUT/SQLITE_PROTO_OUT/CROSS_DIR 等は make の展開値を渡し、Python にパスの別定義を作らない。
check-changed は CC_FILES_JSON を各 run の ctx.files へ渡す。空 stage は検査呼出しを省き、全列へ戻さない。親 make の -j/-k、check_slot、計時、tree save/verify を維持する。
CI の `.github/workflows/check.yml:68` 以降28入口と `build.yml:145` の check-fast は同じ名前。
check-map は新 lint、check-check-select-host は新 test_run の入口。列挙失敗は空列として扱わない。

## 状態・失敗

状態は1呼出しの registry/Plan と lru。入力変更後に選択結果を流用しない。
未知名・引数・宣言不正は rc=2、lint は1、検査不合格は非0。準備失敗なら検査へ進まない。自動再試行・段4への実行やり直しはしない。
`dialect_variants_needed` は旧 `tools/check_select.py:1521` から移す。自己参照は run.py に替える。
旧 make_narrow の実行方針は本番へ移さない。比較時の旧実装は convert の固定 snapshot だけに置く。

## 試験 (tools/checks/test_run.py、R1(a) の所有)

旧 test_check_select.py/test_checkinfra.py から必要なケースを移す。消える形式の相互照合、永続キャッシュ、pack の試験は移さない。
標準 unittest と小さな git/make fixture を使い、root・環境をケースごとに分ける。新しい製品検査・変異数は足さない。

| 観点 | 確認 |
|---|---|
| AST/lint | import 副作用なし、名前・入力・成果物、mutate_tier 省略で既定2。列外7入口を含めない |
| 変更と依存 | 明示空、基点枝、未追跡、削除/rename両名、直接.h/include連鎖/inactive include、別root、globの新規入力 |
| 当たり | tier2/3 は段1でも当たれば実行、tier4 は除外。全検査で ** だけでは unmatched を解消しない |
| 段と変異 | 単一leafの --select は段1、同じ変更の check-changed は段2で当たりに変異。段3の非当たり共有は正常系のみ |
| 分岐 | fast の全列・変異なし、docs の既存集合、full/KAPI/build の段4、unmatched の段3、notestのみ無当たりは準備も検査も0 |
| notest | 当たりがあれば消さない。段の下限2が文書/空notestを引き上げない |
| 静的 | 元の変更パスを渡す。None/空/列、P1bの3本と未対応2本の呼出し契約 |
| 実行/make | 複数行の失敗、MUT/MUTS、host32先頭だけ変異、共有前提の準備1回、ver-info閉包、空列、直接名、計時とrc |

fast/docs/full/段による旧方針との差は上の事例で見る。200件比較へ旧 STAGE1/MUT1 の再現器を足さない。
通常系10秒以内の既存目標は測定で示し、超過を新たな失敗条件にしない。200件比較と長い実検査は毎回の test_run に入れない。
