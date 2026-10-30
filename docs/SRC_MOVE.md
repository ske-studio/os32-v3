# ソースの付け替え手順 (ファイルの置き場を変える)

> 状態: **現行 (2026-10-30)** — ソースファイルの**置き場だけ**を変える (中身・名前・リンク順は変えない) ときの手順の正典。
> 最初の適用は [TASK_SRC_LAYOUT](tasks/v3/TASK_SRC_LAYOUT.md) (L0〜L3)。
> 関数をファイルに切り分ける手順は別 ([POLICY_DEV](POLICY_DEV.md) §2「分割の手段」)。ディレクトリ木の正典は [08_build](08_build.md) §8-3。

## 1. 考え方

- 手で書くのは**対応表 JSON だけ**。`git mv` と参照の置換は道具が行う。人が読むのは対応表と、道具が置換しきれなかった所と、ビルドの `.mk`・`config.mk` の差分。
- 製品が変わっていないことは**成果物のバイト比較**で示す。試験は実装者は回さず、取り込みの検査で 1 回だけ回る (ユーザー了承 2026-10-30)。
- 実装者は terra でよい (中身を書かない作業。「1,000 行超のファイルは terra に出さない」は編集の決まりで、置き場の移動には当たらない)。道具そのものを変える段は sol。

## 2. 道具 (どれも L0 = 5e346e2d で入った)

| 道具 | 役目 |
|---|---|
| `tools/move_files.py <対応表.json> [--dry-run]` | `git mv` と、追跡ファイル全体での旧パスの置換 (完全一致・語境界、`../` 付きは新しい位置から計算し直す、`build/out/obj/` などの出力先つきの `.o/.d` も置換)。置換しきれない形 (分割したパス `ROOT / 'kernel' / 'x.c'`・未認識の接頭辞) は**一覧に出す**だけなので手で直す。dry-run は追跡ファイル全体 (約 82 万行) を見るので数分かかる |
| `tools/include_resolve.py` | 全 `#include "x"` の解決先を JSON に出す。前後で比べ、パスの付け替え以外の変化が無いこと |
| `tools/build_compare.sh <基点> <対象>` | 2 つの木を固定の build_id・`SOURCE_DATE_EPOCH` で組み、kernel・各 .mod・kdebug と r1 の同じもの (計 28 本) をバイト比較。ずれたら objdump で関数ごとの差を出す |
| `tools/source_roots.py` | 走査型の検査 (check_p2v・check_arm_compile・check_arch_asm・check_kprintf_window) の走査の根の一覧。新しい最上位のディレクトリはここに足す |
| `tools/tests/kernel_includes.py` | ホスト試験がカーネルのソースを組むときの -I。`build/config.mk` の `INC_KERNEL` を make で展開するので、**INC_KERNEL に足せば試験は追従する** |

## 3. 手順 (1 段)

1. **前提**: `build/config.mk` の新しいディレクトリの一覧が 1 つの変数 (`KERNEL_SRC_DIRS`) にあり、各 `INC_*` はそれを引く形になっていること (L1 では行ごとに書き足したので、L2 の最初にまとめる — 並びを変えずに、成果物のバイト一致で確かめる)。
2. 対応表 JSON を書く (`[{"from": "kernel/paging.c", "to": "mm/paging.c"}, …]`)。名前は変えない。
3. `python3 tools/move_files.py <表> --dry-run` → 置換の件数と「置換しきれない一覧」を見る → 適用。
4. 一覧に出た所を手で直す (ビルドの `*-module.mk`・`kdebug.mk`・`os32.ld` のパスで選ぶ規則、試験の分割パスなど)。直したファイルと行を報告に書く。
5. 新しいディレクトリを `KERNEL_SRC_DIRS` (→ INC_KERNEL ほか) と、最上位なら `tools/source_roots.py` に足す。
6. 生成物は入力 (`kernel/module/kdebug_imports.def`・`hwdev/hwdev_imports.def`・`tools/prototypes/*_imports.def`) の置換の後に作り直す。古い `.d` が消えたファイルを指して make が止まったら `build/out/obj` の `.d` を消して組み直す。
7. 完了条件 (下) を満たして報告。

### 完了条件 (実装者 — 試験は回さない)

- `make all` rc=0
- `tools/build_compare.sh <基点> HEAD` で 28 本がバイト一致。**例外**: 部品の取り決めの検算値 (`*_MODULE_ABI_GEN`・HWDEV の contract CRC) は契約ヘッダの**本文**から計算するので、道具が註のパスを置換すると値が変わる。このときは「成果物の大きさが前後で同じ」かつ「関数ごとの差が検算値の定数の入れ替えだけ (`movl`・`cmpl` などの即値 4 バイト。HWDEV の CRC は照合の `cmpl` にも出る、L2)」で一致とみなす。成果物ごとの差のバイト数は 8〜40 程度 (L1・L2 の実績)。宣言の置き場 (`source`) は 39ce3607 で `abi_hash` から外したので、置き場だけでは値は変わらない
- `tools/include_resolve.py` の解決先が前後で一致 (パスの付け替えを除いて)。同名ヘッダの解決先も確かめ、比べ方は**単位 (コンパイル単位・ファイル) ごとの解決先の集合**で — 生成ヘッダは include をパスの順に並べるので行番号では比べない (L2)。製品の単位で変化 0 件。試験の fixture で、前は解決しなかった include が同じディレクトリに来て解決するようになるのは差に数えない
- 走査型の検査の走査ファイル数が前後で一致
- `tools/tests`・`build/`・`tools/checks` で旧いパスと文字列のディレクトリ名を grep して 0 件 (道具の一覧の取りこぼしの確かめ)
- `make check-docs-links MUTATE=0` rc=0 (見出しのパスが変わってリンクの断片が切れるのを拾う、L2)
- 変異のコピーの階層と -I の順・分割パス・生成物の隣接の前提を確認し、config.mk を読む道具は make 展開を使う
- `python3 -B tools/checks/run.py --lint` rc=0

### PM の取り込み

対応表・`.mk`・`config.mk` の差分と比較の結果を PM が読む (道具の出力そのものはレビューに出さない — 道具は L0 で astra のレビュー済み)。取り込みの検査は `check_wt.sh` (移動の量なので全体の検査になる)。落ちた試験・検査は、同じ実装者か PM の Opus サブエージェントに直させ、落ちた対象だけ回し直す。rc=0 で push、CI の build を待って次の段へ。
