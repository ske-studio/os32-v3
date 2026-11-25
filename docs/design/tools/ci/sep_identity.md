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
- P2 の 2 木比較では各像の `boot.iso` の存在を確認する。Open Firmware 用の HFS パーティションを作る genisoimage は epoch を固定しても媒体に作成時刻を残すため、ISO 全体のバイト比較から外す (G11)。中に収める `PPCKERN.ELF` は比較する。
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

## 6. 作り直しの枝で保存した一覧と比べる (P2-1)

P2-0 が採った全像の一覧を `sha256sum` の形式 (小文字の SHA-256、空白 2 個、基点の木からの相対パス) で 1 ファイルにまとめる。基点の生成物は一覧の隣に同じ相対パスで保存する。候補の木は古い生成物が残らない所で組む。

    python3 -B tools/build_compare.py --baseline-build --tree BASE_TREE --base v3-pre-restructure -- make all kernel-r1
    python3 -B tools/ci/sep_identity.py --manifest BASE/SHA256SUMS --move-map docs/tasks/restructure/data/move_map.json --tree TREE --base v3-pre-restructure -- make all kernel-r1

1 行目はタグをチェックアウトした空のビルド木での採取、2 行目は候補の比較。両方とも基点 SHA の先頭 12 桁を OS32_BUILD_COMMIT、基点のコミット時刻（Unix 秒）を SOURCE_DATE_EPOCH に設定し、TZ=UTC・LC_ALL=C で GCC の __DATE__・__TIME__ を固定する。末尾のコマンドは木を組む工程 (例は x86 部分のみ)。全件の関門では配布物と PowerPC の 6 像まで作る工程を渡す。組み済みの木を比べ直す入口は `tools/build_compare.py` で、同じオプションから末尾のビルドコマンドを除く。どちらも基点を組み直さない。像のディレクトリを保って移動元のソースに対応するオブジェクトを探す。一覧と同じ場所の `PRESENCE` は、存在だけを確認する基点相対パスを 1 行に 1 本ずつ記す。ハッシュ一覧と重なる物も存在だけを見る（G11）。

一致は終了 0、バイト差・余分な生成物は 1、ビルド失敗・入力の誤りは 2、一覧の生成物の欠けは 3。欠けと差が混在すれば両方を印字して 3 で終わる。
ビルド時は `--base`（既定は `v3-pre-restructure`）、組み済みの木では明示した `--base` から HEAD までのコミット本文を読む。差を許す宣言は 1 行に生成物 1 本とパス 1 本を記す。

- `Sep-Changes: path-string ARTIFACT OLD_PATH` は、保存した基点のハッシュを照合し、旧パスを対応表で同時に置換した全バイトと候補を比べる。テキストの注記にも適用でき、複数のパスには同じ生成物の宣言を繰り返す。長さが変わる ELF には次の再コンパイルを使う。
- ELF の配置まで動く C の診断文字列には、候補でそのリテラルを書く入力も指定する。例は `Sep-Changes: path-string build/out/kdebug/kselftest.o include/memmap.h kdebug/kselftest/memmap.inc`（末尾は移動後の置き場に合わせる）。候補のソース・フォントを一時ディレクトリに写し、同じ固定環境で `make 対象` を回す。候補の全バイトを再現でき、指定の入力の文字列末尾だけを旧パスに戻して組むと保存済み SHA-256 に戻る場合に一致とする。これにより節の配置・シンボル・再配置を含めて検証する。対象は単独で make できる生成物と、ソースに直接書かれた通常の C 文字列。ビルドの失敗は終了 2、再現できない差は終了 1。

- NASM の入力名が ELF に残る場合は `Sep-Changes: path-string userland/tests/kstr_asm_a.o lib/kstring_asm.asm sys/kern/kstring_asm.asm` の形で宣言する。同じソースの写しを旧名でも用意し、候補のレシピのアセンブラ入力名だけを替えて組む。両方の全バイトを照合する。
