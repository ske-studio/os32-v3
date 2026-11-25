# sys/fs/vfs.c — パスの種別判定 (`vfs_path_kind`)

> 状態: **現行 (2026-11-18)** — 出所: HSYNC_IMPROVEMENT_PLAN (H1 の B6 / B7)・TASK_FS_TYPE の契約を今のソースと照らして要約
ソース: `sys/fs/vfs.c` (`vfs_path_kind`・`vfs_kind_probe_cb`・`vfs_chdir`)・`sys/fs/vfs_fd.c` (`vfs_open_probe`)

`vfs_path_kind` はパスがディレクトリか通常ファイルかを返す。戻り値は `VFS_KIND_DIR` / `VFS_KIND_FILE` / 負値 (`VFS_ERR_*`)。
原則は 1 つ: **「読めなかった」を「無い」「その型ではない」と読み替えない。** 負値は判定できなかったことを表し、受け手もそれを通す。

## 判定の順

1. パスの解決に失敗 → そのエラー。マウントが無い → `VFS_ERR_NOMOUNT`。資源の準備 (`vfs_resource_prepare`) の失敗 → そのエラー。
2. マウント点そのもの → `VFS_KIND_DIR` (ドライバに聞かない。FAT の stat がボリュームの根で失敗しても影響しない)。
3. ドライバが `stat` を**持つ** → その答えが最終判断。成功なら `st_mode` で DIR / FILE。**失敗したらそのエラーを返し、下のプローブへ落とさない。**
4. `stat` を**持たない**ドライバだけのプローブ:
   - `list_dir` が成功 → DIR。`NOTDIR` / `NOTFOUND` なら次へ。**それ以外 (`IO`・`FULL` など) はそのまま返す。**
   - `get_file_size` が成功 → FILE。`ISDIR` → DIR (サイズ取得がディレクトリを断った = 種別が分かった)。`NOTFOUND` なら次へ。それ以外はそのまま返す。
   - どれでもなければ `VFS_ERR_NOTFOUND`。

「stat 未対応」はドライバが `stat` を持たないことで表す。stat の失敗を未対応と見なしてプローブへ落とすと、
`get_file_size` はディレクトリでも成功し得るので、読めなかっただけのディレクトリが FILE に化ける。
ext2・FAT・ISO9660・HostDrv はどれも `stat` を持つので、プローブは `stat` を持たないドライバのためだけにある。

## 受け手

種別の判定は、それを**消費する側**まで通して確かめる (戻り値だけを見る試験では受け手の追従漏れが分からない)。
カーネル内の受け手は 2 つ。

| 受け手 | 扱い |
|---|---|
| `vfs_open_probe` (`vfs_open`・`vfs_open_sqlite` など全部の open が通る) | DIR → `VFS_ERR_ISDIR`。`NOTFOUND` → 続行 (`O_CREAT` の作成経路がある)。**それ以外の負値 → そのエラーを返して FD を出さない** (`O_CREAT` でも作らない)。詳しくは `vfs_fd_open.md` |
| `vfs_chdir` | 負値はそのまま返して cwd を変えない。DIR 以外は `VFS_ERR_NOTDIR` |

`vfs_rm`・`vfs_rename`・`vfs_rmdir`・`vfs_read`・`vfs_write` は種別を判定せず、ドライバの型検査に任せる。
ユーザーランドの種別判定は `sys_stat` を正にする別系統 (`docs/design/userland/shell/cmd_fs_shared.md`)。
