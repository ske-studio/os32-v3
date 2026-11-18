# fs/vfs_fd.c — open の作成・切り詰め・排他的作成

> 状態: **現行 (2026-11-18)** — 出所: TASK_FS_TYPE (B8)・TASK_H2 (O_EXCL) の契約を今のソースと照らして要約
ソース: `fs/vfs_fd.c` (`vfs_open_internal`・`vfs_open_probe`)・`fs/vfs.h` (`VfsOps.create_excl`)・`fs/ext2/ext2_vfs.c` (`ext2_vfs_create_excl`)・`sdk/include/os32/os32_kapi_shared.h` (`KAPI_O_EXCL`)

`vfs_open`・`vfs_open_sqlite` などの open はすべて `vfs_open_internal` を通る。
原則: **作成へ進めるのは「本当に無い」と分かったときだけ。** 読めなかったことを「無い」と読み替えると、
`O_CREAT` の作成 (空ファイルの書き込み) が既存の中身を消す。

## `O_EXCL` が無い open (`vfs_open_probe`)

| 段 | 条件 | 結果 |
|---|---|---|
| 1 | `vfs_path_kind` が DIR | `VFS_ERR_ISDIR` |
| 1 | `vfs_path_kind` が `NOTFOUND` 以外の負値 | **そのエラー** (FD を出さない、作らない) |
| 2 | ドライバに `get_file_size` が無い | `VFS_ERR_INVAL` |
| 3 | `get_file_size` が `NOTFOUND` 以外で失敗 | **そのエラー** (種別が FILE と確定していても、`O_CREAT` でも作成へ進まない) |
| 3 | `get_file_size` が `NOTFOUND`、`O_CREAT` 無し | `VFS_ERR_NOTFOUND` |
| 3 | `get_file_size` が `NOTFOUND`、`O_CREAT` あり | 長さ 0 の `write_file` で作る。**その失敗はそのまま返す** |
| 4 | inode を持たない FS で `O_TRUNC` (書き込みモード) | 長さ 0 の `write_file`。**戻り値を見る** (失敗したら open しない) |

- 作成へ進めるエラーは `NOTFOUND` だけ。`O_CREAT` あり・`O_TRUNC` なし (「無ければ作る、あれば開く」) は最も普通の書き込み用途なので、
  ここで既存の中身が消えるのが最も重い壊れ方。
- inode を持つ FS (ext2) は inode を引いてから inode で切り詰める (`ino->truncate`、ディレクトリは `ISDIR`)。
  inode が取れない・切り詰めが失敗したら open を失敗にし、この open で作ったものは消す。
- 正常系: 通常ファイルは開ける、ディレクトリは `ISDIR`、本当に無ければ `O_CREAT` で作れる、`O_TRUNC` は切り詰める、
  `O_CREAT` だけなら既存の中身は残る。

## 排他的作成 `O_EXCL` (KAPI v53)

`KAPI_O_EXCL` は 0x0400。判定の順が契約の一部。

| 順 | 条件 | 結果 |
|---|---|---|
| 1 | `O_CREAT` が無い (`O_EXCL` 単独、`O_RDONLY \| O_EXCL` も) | `VFS_ERR_INVAL`。何も呼ばない |
| 2 | ドライバが `create_excl` を持たない | `VFS_ERR_NOSYS`。**種別検査 (`vfs_path_kind`・`get_file_size`・`list_dir`) より前** — ディレクトリでも、stat が I/O で落ちる場合でも NOSYS が先 |
| 3 | 相対パスがマウントの根 | `VFS_ERR_EXIST` |
| 4 | `create_excl` を呼ぶ | 下の 3 値をそのまま返す |

経路の解決とマウントの確認は 1 より前 (マウントの外は `NOMOUNT`)。

`create_excl(ctx, path)` の契約 (`fs/vfs.h`): 「無いことの確認 → 作成」を**1 回の呼び出しの中で**行う。

- `VFS_OK` — 長さ 0 の通常ファイルを作った。open はサイズ 0 の FD を返す (`get_file_size` を通らない)。`O_TRUNC` を併せても余計な書き込みをしない。
- `VFS_ERR_EXIST` — 名前が既に在る。**種別を問わない** (ディレクトリでも `ISDIR` ではなく `EXIST`)。作らない・切り詰めない。
- その他の負値 — 在るかどうかを判定できなかった (`IO`・`ROFS`・`NOSPC` など)。**「無い」と読み替えずにそのまま返す。**

作成は `create_excl` だけが行い、`write_file` 経由の作成は通らない。
排他性の根拠は VFS が非再入でゲストが協調型であること。ホスト側が同時に書ける FS では成り立たないので、
実装は ext2 だけ (`ext2_vfs_create_excl` が親を解決して `ext2_create` を長さ 0 で呼ぶ。`ext2_create` の存在確認は 3 値)。
HostDrv・FAT・ISO9660 は持たず、`O_EXCL` は `NOSYS` になる。

`O_EXCL` の無い open は従来どおり (上の表)。`sys_open` の意味が広がったので KAPI の版を上げたが、スロットは増えていない。
