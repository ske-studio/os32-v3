# userland/shell/cmd_fs_shared.c — シェルの種別判定

> 状態: **現行 (2026-11-18)** — 出所: HSYNC_IMPROVEMENT_PLAN (H1 の B5)・TASK_FS_TYPE の契約を今のソースと照らして要約
ソース: `userland/shell/cmd_fs_shared.c` (`fs_path_kind`・`fs_is_dir`・`fs_ls_says_dir`)・`userland/shell/cmd_fs_shared.h` (`FS_KIND_FILE`・`FS_KIND_DIR`)

シェルのコマンドがパスの種別を知るための口。**型で判定し、列挙の成否を種別の代わりにしない。**
列挙は 1000 件を超える (`FULL`) / 途中で切れる (`IO`) ディレクトリでも失敗するので、
それを「ディレクトリではない」と読むと `cp -r` が宛先の階層を取り違える。

## `fs_path_kind(path)`

| 状況 | 戻り値 |
|---|---|
| `sys_stat` が成功 | `FS_KIND_DIR` (1) / `FS_KIND_FILE` (0) |
| `sys_stat` が `OS32_ERR_NOTFOUND` | `OS32_ERR_NOTFOUND` (無いと分かった) |
| `sys_stat` がそれ以外で失敗し、列挙がディレクトリと答えた | `FS_KIND_DIR` |
| それ以外 | **`sys_stat` のエラーをそのまま** (`NOSYS`・`IO` など。`NOTFOUND` に畳まない) |

- `sys_stat` が正。列挙 (`fs_ls_says_dir`) は stat が使えない FS のための代わりにだけ使う。
- `fs_ls_says_dir` は 3 値: 1 = ディレクトリ / 0 = ディレクトリではない (`NOTDIR`・`NOTFOUND`) / -1 = 読めなかった (`IO`・`FULL` など)。
  -1 を 0 に畳まない。

## `fs_is_dir(path)`

`fs_path_kind(path) == FS_KIND_DIR` の真偽。種別が分からないときは 0 で、**「不明」を運べない**。
不明を扱う必要がある呼び手 (cp・mv・rm) は `fs_path_kind` の負値を見る (`docs/design/userland/shell/cmd_file.md`)。
真偽で足りる新しい呼び手だけが `fs_is_dir` を使う。
