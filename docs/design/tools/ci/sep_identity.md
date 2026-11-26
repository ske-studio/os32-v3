# tools/ci/sep_identity.py — 機種依存の分離の枝専用の検査 (SEP_IDENTITY_DESIGN)

> 状態: **現行 (2026-11-27)** — ソース移動時の比較と、P3 の生成物の再現。
ソース: `tools/ci/sep_identity.py`、`tools/build_compare.py`、`tools/ci/replay/`。

## 1. なぜ要るか

配置変更の前後で、同じ設定から組んだ配布物と PowerPC のオブジェクトを照合する。現在の取り込み関門は CI 設計 §0・§10 に従う。分離枝で比較と変異を自動実行した時の記録は `git show dafe81642:docs/design/tools/ci/sep_identity.md` にある。

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
- 宣言はその段の受入のやり方を変える印でもある: `x86` を宣言した段は NP21/W の受入、`ppc` を宣言した段は PowerPC の QEMU 起動 で確かめる。

## 4. 変異込みの検査

必須変異は候補 SHA の登録と CI の選択計画から得る。通常の検査、変異とその結果の照合は `python3 -B -m tests.infra.ci` が担う。

## 5. 範囲の外

生成物の比較は起動の受入を含まない。取り込み時の PC-98 と PowerMac の起動記録は CI 設計 §8 に従う。

## 6. 作り直しの枝で保存した一覧と比べる (P2-1)

ソース名は C の診断文字列や NASM のオブジェクト名、Rust の診断情報にも入る。作業木の絶対パスやビルド時刻が混じる比較では、入力を固定して実際の生成物を照合する。

P2-0 が採った全像の一覧を `sha256sum` の形式 (小文字の SHA-256、空白 2 個、基点の木からの相対パス) で 1 ファイルにまとめる。基点の生成物は一覧の隣に同じ相対パスで保存する。候補の木は古い生成物が残らない所で組む。

    python3 -B tools/build_compare.py --baseline-build --tree BASE_TREE --base v3-pre-restructure -- make all kernel-r1
    python3 -B tools/ci/sep_identity.py --manifest BASE/SHA256SUMS --move-map tools/ci/replay/move_map.json --tree TREE --base v3-pre-restructure -- make all kernel-r1

1 行目はタグをチェックアウトした空のビルド木での採取、2 行目は候補の比較。両方とも基点 SHA の先頭 12 桁を OS32_BUILD_COMMIT、基点のコミット時刻（Unix 秒）を SOURCE_DATE_EPOCH に設定し、TZ=UTC・LC_ALL=C で GCC の __DATE__・__TIME__ を固定する。末尾のコマンドは木を組む工程 (例は x86 部分のみ)。全件の関門では配布物と PowerPC の 6 像まで作る工程を渡す。組み済みの木を比べ直す入口は `tools/build_compare.py` で、同じオプションから末尾のビルドコマンドを除く。どちらも基点を組み直さない。像のディレクトリを保って移動元のソースに対応するオブジェクトを探す。一覧と同じ場所の `PRESENCE` は、存在だけを確認する基点相対パスを 1 行に 1 本ずつ記す。ハッシュ一覧と重なる物も存在だけを見る（G11）。

一致は終了 0、バイト差・余分な生成物は 1、ビルド失敗・入力の誤りは 2、一覧の生成物の欠けは 3。欠けと差が混在すれば両方を印字して 3 で終わる。
ビルド時は `--base`（既定は `v3-pre-restructure`）、組み済みの木では明示した `--base` から HEAD までのコミット本文を読む。差を許す宣言は 1 行に生成物 1 本とパス 1 本を記す。

- `Sep-Changes: path-string ARTIFACT OLD_PATH` は、保存した基点のハッシュを照合し、旧パスを対応表で同時に置換した全バイトと候補を比べる。パスは参照の先頭から末尾まで一致するものに限る（前後がデータの端か、ASCII 英数字・`_`・`-`・`.`・`/` 以外のバイト）。テキストの注記にも適用でき、複数のパスには同じ生成物の宣言を繰り返す。長さが変わる ELF には次の再コンパイルを使う。
- ELF の配置まで動く C の診断文字列には、候補でそのリテラルを書く入力も指定する。例は `Sep-Changes: path-string build/out/kdebug/kselftest.o sys/include/memmap.h sys/platform/pc98/kdebug/kselftest/memmap.inc`（末尾は移動後の置き場に合わせる）。候補のソース・フォントを一時ディレクトリに写し、同じ固定環境で `make 対象` を回す。候補の全バイトを再現でき、指定の入力の文字列末尾だけを旧パスに戻して組むと保存済み SHA-256 に戻る場合に一致とする。これにより節の配置・シンボル・再配置を含めて検証する。対象は単独で make できる生成物と、ソースに直接書かれた通常の C 文字列。ビルドの失敗は終了 2、再現できない差は終了 1。

- NASM の入力名が ELF に残る場合は `Sep-Changes: path-string userland/tests/kstr_asm_a.o sys/kern/kstring_asm.asm sys/kern/kstring_asm.asm` の形で宣言する。同じソースの写しを旧名でも用意し、候補のレシピのアセンブラ入力名だけを替えて組む。両方の全バイトを照合する。

## 7. 理由つきの差を旧い入力で再現する (`reviewed-replay`、P3)

宣言の形式は `Sep-Changes: reviewed-replay ARTIFACT CANDIDATE_SHA256 REASON`。基点相対の生成物名、候補の SHA-256（小文字 64 桁）、理由区分（`path`・`hwdev`・`module-abi`・`combined`）を順に書く。比較器は候補を宣言のハッシュと、再現木を基点のハッシュと照合する。P3 の候補は `2604e4964b9c`、基点は `v3-pre-restructure`。93 件の内訳はパス 24、HWDEV の CRC 7、部品の ABI の世代 20、複合 42。[照合表](../../../../tools/ci/replay/reviewed-differences.tsv)

U20 の再現には §6 の固定環境と手元の基点一覧を使う。以下の `p` は P3 の記録を複製した作業用ディレクトリの絶対パス、`repo` は台本を収めた現在のリポジトリの絶対パスとする。`$p/tmp/verified-tree` に組み済みの候補、`$p/rebuild-final.log` にそのビルドログ、`$p/compare-final.log` に宣言適用前の差を置く。隣の `base/` に基点の一覧と生成物、`p2-0/first-build.log` に基点のビルドログを置く。

1. 候補の Git 作業木で契約値を再計算し、新しい再現木に候補を複製する。

       scripts="$repo/tools/ci/replay"
       export TMPDIR="$p/tmp"
       cd "$p/tmp/verified-tree"
       python3 -B "$scripts/contract-replay.py" > "$p/module-abi.log"
       cp -a . "$p/tmp/replay-tree"
       python3 -B "$scripts/replay-normalize.py" "$p"

2. 正規化は生成済み契約の値と kselftest の診断文字列を戻し、NASM の新旧入力名をログから対応づける。当時の追加補正では、再現木の `userland/diag/hw/{gf_palette,gf_regs,tri_burst4}.hwb`、`hsync.1`、SDK の配布テキストとその入力のパス表記も `move_map.json` の移動元に戻して基点のバイトと照合した。`userland/diag/kstr_asm_a.o` を再現木から除き、直接アセンブルする kstring も含めた保存版ラッパーを `AS` に渡す。

       cd "$repo"
       python3 -B tools/build_compare.py --baseline-build --tree "$p/tmp/replay-tree" --base v3-pre-restructure -- make AS="$scripts/nasm-replay" all kernel-r1

3. ビルドで再配布された `build/sdk/` の `bin/os32x_hdr.py`、`include/rt/testresult.h`、`include/utf8.h`、`rust/os32api/src/gui/stub.rs` にも同じ旧パス表記を適用する。次に台本で93件を照合し、出力した宣言を比較器に渡す。

       python3 -B "$scripts/audit-comparison.py" "$p"
       cut -f1-5 "$scripts/reviewed-differences.tsv" | cmp - "$p/reviewed-differences.tsv"
       python3 -B tools/build_compare.py --manifest "$p/../base/SHA256SUMS" --move-map "$scripts/move_map.json" --tree "$p/tmp/verified-tree" --declarations "$p/sep-changes.txt" --replay-tree "$p/tmp/replay-tree"
