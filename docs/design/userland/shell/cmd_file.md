# userland/shell/cmd_file.c — cp / mv / rm の種別の扱いと cp -r

> 状態: **現行 (2026-11-18)** — 出所: HSYNC_IMPROVEMENT_PLAN (H1 の B5)・TASK_FS_TYPE の契約を今のソースと照らして要約
ソース: `userland/shell/cmd_file.c` (`file_kind_or_refuse`・`FILE_KIND_UNKNOWN`・`cmd_cp`・`do_copy_recursive_impl`・`do_move_one`・`cmd_mv`・`cmd_rm`)

種別の判定そのものは `userland/shell/cmd_fs_shared.c` の `fs_path_kind` (`docs/design/userland/shell/cmd_fs_shared.md`)。
この文書は、その答えを受ける側が「読めなかった」を「ファイル」「無い」と読み替えないための約束。

## 種別が分からないときは断る (`file_kind_or_refuse`)

`fs_path_kind` の戻り値が `FS_KIND_DIR` / `FS_KIND_FILE` / `OS32_ERR_NOTFOUND` ならそのまま返す。
それ以外の負値は `<cmd>: cannot determine the type of '<path>': <strerror>` を出して返す (`FILE_KIND_UNKNOWN`)。
`NOTFOUND` は「無いと分かった」なので断りにしない (従来の `cannot open` / `cannot remove`、無い名前への cp / mv)。

| 箇所 | 分からないとき |
|---|---|
| `cmd_cp` の宛先 | コマンド全体を打ち切る (`open`・`mkdir` を呼ばない)。複数入力でも「ディレクトリでない」と言い切らない |
| `cmd_cp` の入力 | その 1 件だけ飛ばす (分かる入力は写す) |
| `do_move_one` (rename が `INVAL` = FS をまたぐ) の入力 | コピーも `unlink` もしない (原本が残る)。無い (`NOTFOUND`) ときも進まない |
| `cmd_mv` の宛先 | コマンド全体を打ち切る (`rename` しない) |
| `cmd_rm` の対象 | その 1 件の `unlink` を呼ばない (読めないディレクトリを unlink へ渡さない) |

断ったことは、断りの文言と、`open`・`mkdir`・`rename`・`unlink` の呼び出しが 0 回で FS の中身が変わらないことで確かめる。
コマンドは失敗を状態 (`SH_STATUS_ERROR`) で返す。

## `cp` の宛先の階層

- 宛先が**ディレクトリ** (stat が DIR) なら、入力の basename を足した `<宛先>/<名前>` へ写す。
  `cp -r /src /big` は `/big/src/…` へ入り、`/big` の直下の既存ファイルを上書きしない。宛先の列挙が `FULL` / `IO` でも変わらない。
- 宛先が**無い**なら、その名前そのものへ写す (basename を足さない。`cp -r /src /newdir` は `/newdir` の直下へ)。
- 単一ファイルをディレクトリへ写すときも `<宛先>/<名前>` で、宛先のディレクトリをファイルで潰さない。
- 入力が複数で宛先がディレクトリでなければ `multiple files must be copied into a directory`。

## `cp -r` の 1 段 (`do_copy_recursive_impl`)

順序は **収集 → 件数の確認 → `mkdir`**。

1. 列挙して名前を集める。`sys_ls` が負なら `cp -r: cannot read directory '…'` で戻る (宛先を作らない)。
2. `MAX_COPY_ENTRIES` (64) を超えたら `cp -r: too many entries in '…' (max 64)` で戻る (**`sys_mkdir` を呼ばない** = 空のディレクトリを残さない)。
3. `sys_mkdir(dst)`。0 以外なら `cp -r: cannot create directory '…'` で**中へ進まない**。
   ただし `OS32_ERR_EXIST` **かつ** `fs_path_kind(dst) == FS_KIND_DIR` なら通す (`cp -r a b` を 2 回打つ使い方を壊さない)。
   同名のファイルがある・型が分からないときは断る。最上段の宛先の型検査は `cmd_cp` が見るが、`<宛先>/<名前>` や再帰の内側の宛先はここでしか見ない。
4. 集めた項目を写す。内側の種別は列挙の `type` を使う。

## 範囲の外

- 64 件を超える階層の途中まで写した子は残る (断りは出る)。
- `sys_mkdir` が既存のディレクトリに `OS32_ERR_EXIST` を返すことは KAPI の約束 (名前が既に在れば種別を問わず `EXIST`) に依っている。
