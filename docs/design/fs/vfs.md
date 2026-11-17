# fs/vfs.c — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_EXT2_EMPTY_NAME の契約を今のソースと照らして要約
ソース: `fs/vfs.c` (`vfs_rel_is_root`・`vfs_write`・`vfs_rm`・`vfs_rename`・`vfs_mkdir`・`vfs_rmdir`)・`fs/vfs_fd.c` (`vfs_open_internal`)

VFS の API の一覧・パスの長さ・最終要素の `.` / `..`・FD の失効の正典は `docs/06_filesystem.md` §6-1。ここはマウント点そのものへの操作だけを書く。

## マウント点そのものへの操作はドライバへ渡さない

`vfs_resolve_path` が末尾の `/`・`//`・`.` を畳むので、`/hd0`・`/hd0/`・`/hd0//`・cwd が `/hd0` のときの `""` は、`vfs_route` の後の相対パスがどれも `"/"` になる (`vfs_rel_is_root`)。この形は FS のルートで、名前を持たない。ドライバへ渡すと、親 `/` + 名前 `""` に分かれて名前の無い項目を作ったり、ルートを消そうとしたりする。そこで VFS が先に断り、**ドライバには 1 回も届かない**。

| 操作 | マウント点そのものなら |
|---|---|
| `vfs_mkdir` | `VFS_ERR_EXIST` (`mkdir("/")` と同じ。ルートは必ず在る) |
| `vfs_write` | `VFS_ERR_ISDIR` |
| `vfs_rm` | `VFS_ERR_ISDIR` |
| `vfs_rmdir` | `VFS_ERR_INVAL` |
| `vfs_rename` (元・先のどちらか) | `VFS_ERR_INVAL` |
| open (`O_CREAT` 付きも) | 種別が DIR なので `VFS_ERR_ISDIR`、`O_CREAT` と `O_EXCL` の組は `VFS_ERR_EXIST` (`docs/design/fs/vfs_fd_open.md`) |

- `mkdir /hd0/a/`・`/hd0//b`・`/hd0/a//c/` は畳まれて通常の名前になるので作れる (ドライバには `"/a"` などで届く)。
- ext2 側にも同じ守り (`ext2_vfs_mkdir` のルート = EXIST、`ext2_name_check`) があり冗長だが、ext2 以外のドライバのために VFS でも断る。

検査: `make check-ext2-empty-name-host` (`tools/tests/test_ext2_empty_name.py`)。段 `vfs`・`root` が `/hd0` と `/` に載せた ext2 で、段 `synth` が合成ドライバを `/syn` に載せて、ドライバまで届かないことを見る。
