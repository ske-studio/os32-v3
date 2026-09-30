# H1 — `hdrv_list_dir()` の列挙ループ (ホスト試験の記録)

- 票: [`docs/tasks/shell/HSYNC_IMPROVEMENT_PLAN.md`](../../docs/tasks/shell/HSYNC_IMPROVEMENT_PLAN.md)
  §8 の H1 行 — 完了条件「I/O 失敗を成功にしない」/ 対象「HostDrv のエラー処理」
- 上位の記録: [`h1_tdd.md`](h1_tdd.md) — H1 全体の RED→GREEN はそちら
- 実行: `python3 -B tools/tests/test_hostdrv_list.py [--target]`
  (`make check-hostdrv-list-host` が同じものを `--target` 付きで回す)
- 対象: `fs/hostdrv_list_rules.inc` (実物を `#include`) / `fs/hostdrvfs.c` (クロス確認)

## 0. 正直に書く ([V4])

**この記録は試験と実装の後に書いた (2026-09-15)。** 試験は H1 の作業中に
書かれていて、そのときの RED→GREEN は [`h1_tdd.md`](h1_tdd.md) にある。ここは
それを `docs/TESTS.md` から名前で引けるようにするための記録で、**新しく RED を
踏み直してはいない**。自動の変異 (`--mutate`) も無い。§3 は試験を読んで書いた。

## 1. 何を確かめる試験か

HostDrv の一覧取得は「1 件ずつ問い合わせて、終わりが来るまで繰り返す」形。
ハイパーコールを叩く `fs/hostdrvfs.c` 本体はホストで組めないので、**ループの
規則だけを `fs/hostdrv_list_rules.inc` に純関数として切り出してある**
(票 H1 の `hdrv_stat_fill` と同じ作法)。`fs/hostdrvfs.c` はこの `.inc` を
`#include` して呼ぶだけで、判定の写しをどこにも作っていない。

`tools/tests/hostdrv_list_host.c` はその `.inc` を 1 行も写さずそのまま
`#include` し、`hostdrv_query_dir` に当たる **1 件取得だけ**を台本式の贋物に
差し替える。注入するのは 2 つ:

- **(a) 途中で負値** — 200 件のうち 50 件目で失敗する
- **(b) 件数上限での打ち切り** — 5000 件を上限 1000 で読む

守らせたいのは 1 つだけ: **部分的な一覧を `VFS_OK` で返さない**。
呼び手 (`hsync`) は `rc != 0` を見て「このディレクトリは同期できなかった」と
言えるが、`VFS_OK` で 49 件だけ返されると**残り 151 件を「存在しない」と
扱ってしまう**。

## 2. GREEN (現状)

```
HOST GNU89 -Werror COMPILE PASS (real fs/hostdrv_list_rules.inc)
23 checks, 0 failures
EXIT hostdrv_list_host=0
TARGET i386-elf -Werror COMPILE PASS (fs/hostdrvfs.c)
```

| 節 | 見ているもの |
|---|---|
| 正常 | 200 件を最後まで流して 0。最初の問い合わせだけ `first=1`、2 回目以降は `first=0` (再開規約) |
| 空ディレクトリ | 0 件でも 0 (誤検出しない) |
| `.` / `..` 相当 | 飛ばしても 0。`emits` 200 に対し `delivered` はスキップぶん少ない |
| (a) 途中の I/O 失敗 | **`VFS_OK` を返さない**。`HDRV_LIST_ERR_IO` (= `OS32_ERR_IO`)、流れたのは 49 件だけ |
| (a) 境界 | 1 件目で失敗しても「空ディレクトリ」と言わない。最後の 1 件で失敗しても失敗 (199 件流れていても 0 にしない) |
| (b) 件数上限 | **`VFS_OK` を返さない**。`HDRV_LIST_ERR_CAPPED` (= `OS32_ERR_FULL`) で **I/O とは区別**する。上限を超えて問い合わせない |
| (b) 境界 | ちょうど上限の件数でも打ち切り扱い (`count++` が先)。上限の 1 つ手前は成功 |
| 引数の防御 | `step` / `emit` が `NULL`、上限 0 は失敗 (**無限ループにしない**) |
| 2 つのエラーは別物 | `ERR_IO != ERR_CAPPED`、どちらも 0 ではない |

`--target` は `fs/hostdrvfs.c` が `i386-elf-gcc -Werror` でも通ること ([C1]) を見る。

## 3. 壊すとどれが落ちるか

| 壊し方 | 落ちる検査 |
|---|---|
| 1 件取得の負値を `break` して `return 0` にする (直す前の形) | 「**`VFS_OK` を返さない**」「`OS32_ERR_IO` を返す」— H1 の中心 |
| 上限に達したら黙って `return 0` | 「**`VFS_OK` を返さない** (黙って切り詰めない)」「`OS32_ERR_FULL` を返す」 |
| I/O 失敗と件数上限を同じ値にする | 「I/O 失敗と件数上限を同じ値にしない」— 呼び手が再試行の可否を決められなくなる |
| 上限を `>` で見る (`>=` のつもりで) | 「ちょうど上限の件数でも打ち切り扱い」または「上限の 1 つ手前は成功」のどちらかが必ず落ちる |
| 上限 0 を許す | 「上限 0 は失敗 (無限ループにしない)」— ゲスト側が固まる |
| `first` フラグを毎回 1 にする | 「2 回目以降は `first=0` (再開規約)」— ホスト側が毎回先頭から返し、**同じ 1 件を無限に読む** |
| 空ディレクトリを I/O 失敗と同じ扱いにする | 「空ディレクトリも 0 (誤検出しない)」 |

## 4. この試験が言わないこと

- **ハイパーコールは 1 度も呼んでいない。** NP21/W の HostDrv が実際に何を
  返すか、`C:\os32` の実物のディレクトリで何件流れるかは見ていない。
- `hsync` 側がこのエラーをどう表示・集計するかは
  [`h1_tdd.md`](h1_tdd.md) / [`h3_tdd.md`](h3_tdd.md) の側。
- HostDrv の `stat` の是正 (`hdrv_stat_fill` / `hdrv_size_result`) は
  [`h1_tdd.md`](h1_tdd.md) と [`b8_tdd.md`](b8_tdd.md) が見る。
