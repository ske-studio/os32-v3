# tools/ci/sep_identity.py — 機種依存の分離の枝専用の検査 (SEP_IDENTITY_DESIGN)

> 状態: **現行 (2026-11-17)** — 出所: 機種依存の分離の作り直し (ユーザーの提案 2026-11-17「ci を今回用の事専用で用意しては？」)。
ソース: `tools/ci/sep_identity.py`、`.github/workflows/sep.yml`、試験 `tools/tests/test_sep_identity.py` (検査 `sep-identity-host`)

## 1. なぜ要るか

分離の作り直しは「移すだけで振る舞いを変えない」段が大半で、確かめは 2 つある。毎回の CI (check.yml・build.yml) はどちらも見ない。

| 確かめ | 毎回の CI | 手元で起きたこと (2026-11-17) |
|---|---|---|
| 生成物が基点と同じ (x86 の約 1,200 個、PowerPC の像の約 950 本の `.o`) | 見ない | 段ごとに手元で 20〜40 分かけて組み比べた |
| ソース・`.mk` の本文を目印にした変異が今も当たる | 変異なしで回すので見ない | 一覧を消した段で目印が消えた試験が 1 本、基点の時点で既にずれていた試験が 2 本、手元の長い回しで見つかった |

`wt/sep-*` の枝に push すると `.github/workflows/sep.yml` がこの 2 つを回す (変異込みの検査の側の設計は `gate.md`)。main への取り込みの判定は今までどおり check・build で、これは分離の枝の追加の関門。

## 2. 生成物の一致 (`identity` のジョブ)

`python3 -B tools/ci/sep_identity.py --work <空の置き場>` が、基点 (`origin/main` との merge-base) と HEAD を比べる。

| 対象 | 比べ方 |
|---|---|
| `x86` | `tools/build_compare.py <基点> <HEAD>` (2 つの写しを組んで、出荷する生成物を比べる。build ID と日時は固定される) |
| `ppc` | 基点と HEAD を `git archive` で写し、それぞれ PowerPC の 6 像 (core・link・sh・link-sh・shell・neg1) を `make ARCH=ppc PLATFORM=powermac` で組んで、`build/out/ppc-kboot` の下の `.o` の sha256 を比べる |

- PowerPC の `.o` は「像・ソースのパス」で対にする。`git diff --name-status -M90` が移動と見たソースは、古いパスの `.o` と対にする (置き場だけ変えた段で「消えた・増えた」にならない)。
- `root_image_blob.o` (RAM root の ISO) は同じ入力でも作るたびに変わるので比べない。カーネルのコードではない。
- 違い (中身が違う・基点にだけ在る・HEAD にだけ在る) があれば落とす。

## 3. 変わる段の宣言

生成物が変わるのが正しい段は、基点から HEAD までのコミットのどれかの本文に、行として書く:

    Sep-Changes: x86
    Sep-Changes: ppc
    Sep-Changes: x86 ppc

- 宣言した対象は、違っても落とさない (違いの一覧は印字する)。宣言していない対象が違えば落とす。
- 宣言したのに同じだった時は通す (その旨を印字する)。
- 行の全体が `Sep-Changes:` で始まる時だけ宣言と読む。`x86`・`ppc` 以外の語は誤り。
- 宣言はその段の受入のやり方を変える印でもある: `x86` を宣言した段は NP21/W の受入、`ppc` を宣言した段は QEMU の検査 (`ppc-qemu` のジョブ) で確かめる。

## 4. 変異込みの検査

同じ workflow の `checks` のジョブが、全部の検査を変異込みで 6 シャードに分けて回す。形と決まりは `gate.md` (全件は main に入れる SHA の関門、`docs/POLICY_CI.md` G1)。

## 5. 範囲の外

NP21/W・実機の受入はしない。分離の作り直しが終わったら、この workflow と道具は削除する。
