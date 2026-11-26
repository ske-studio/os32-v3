# sdk/crt/syscalls.c — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_VFS_FD_PATH (方針 v3 の 7) の契約を今のソースと照らして要約
ソース: `sdk/crt/syscalls.c` (`os32_errno`・`os32_ret`・`_read`・`_write`・`_open`・`_lseek`・`_fstat`・`_stat`・`_unlink`・`_close`)

## KAPI の負のエラーを newlib の -1 + errno に直す

newlib (stdio) は負の戻り値を「-11 バイト書けた」などと読み、errno も立たない。そこで newlib へのシステムコールの口は KAPI の戻り値を `os32_ret` に通す: **非負はそのまま返し、負は errno を立てて -1 を返す。**

| KAPI (`OS32_ERR_*`) | errno |
|---|---|
| `NOTFOUND` | `ENOENT` |
| `EXIST` | `EEXIST` |
| `NOSPC` | `ENOSPC` |
| `NOTDIR` | `ENOTDIR` |
| `NOTEMPTY` | `ENOTEMPTY` |
| `ISDIR` | `EISDIR` |
| `INVAL` | `EINVAL` |
| `NOSYS` | `ENOSYS` |
| `STALE` | `ESTALE` |
| `ROFS` | `EROFS` |
| `NAMETOOLONG` | `ENAMETOOLONG` |
| `BUSY` | `EBUSY` |
| 上にないもの (`IO` を含む) | `EIO` |

- 通す口: `_read`・`_write`・`_open`・`_lseek`・`_fstat`・`_stat`・`_unlink` (と同名の別名)。
- `_close` は `sys_close` の戻りを見ず、常に int 0 を返す。

検査: `make check-vfs-fd-path-host` (`test_vfs_fd_path.py (旧版 3062ccc08e29)` の段 `errno`、本体 `newlib_errno_host.c (旧版 bcd1002cf266)`、実物の `syscalls.c` を newlib のヘッダで)。
