# sys/fs/fat/fatfs_vfs.c — FatFs の VFS 接続

> 状態: **現行 (2026-10-31)**

FatFs を VFS の操作に接続し、パスと結果を変換する。
ディレクトリ列挙や媒体のマウントを扱う。

## 読み取りの寿命と更新

FatFsCtx に [4本の読み取り保持状態](fatfs_vfs_read_cache.md) を所有する。
ff_read_file は従来の ff_make_path の検査後に rc_read を呼び、一括は位置0、
stream は明示 offset を渡す。成功時は実読込長、失敗時は従来の ff_to_vfs の負値を返す。
VFS close から backend への通知は増やさない。

| 契機 | 全保持 FIL を破棄する位置 |
|---|---|
| write/write_stream | パス検査後、変更用 f_open の前 |
| unlink/mkdir | パス検査後、変更用 API の前 |
| rename | 両パス検査後、f_rename の前。祖先や別綴りも全破棄 |
| rmdir | パスと f_stat の種別検査後、f_unlink の前 |
| 書き込み失敗回復 | ff_write_result の既存 f_mount(NULL) の前 |
| mount 失敗・区画再試行・umount | rc_drop_on_umount の後に f_mount(NULL)、最後に ctx 解放 |

更新失敗でも保持を戻さず、書き込み回復の再マウントが失敗しても空のままにする。
パス不正や rmdir の種別不一致では FatFs の変更操作に届かず、保持を維持する。
初回と再利用 ctx は kzalloc で空から始まる。
媒体交換は [ファイルシステムの契約](../../../06_filesystem.md) の umount/mount に従う。

## 落とし穴

- **名前フィールドの境界**: 隣接する配列でも一方の境界を越えて他方を読むと C の未定義動作になり、最適化で名前が壊れうる。
  名前と拡張子を別配列で扱うコードでは各配列を独立して参照する。
  現行の列挙は FatFs が返す `fno.fname` を VFS 名へ写す (`sys/fs/fat/fatfs_vfs.c`)。
  過去の固定長構造を現行 FatFs の契約とみなさない。直接の配列境界回帰は未確認。

## 「在るか」を問う入口の誤りの写像 (`ff_stat_to_vfs`)

> 出所: TASK_S3 (S3-K) の契約を今のソースと照らして要約

- `ffconf.h` は `FF_USE_LFN 0` なので、8.3 に収まらない名前 (例 `settings.db-journal`) は `f_stat` が `FR_INVALID_NAME` を返す。そのボリュームに存在しえない名前は stat の意味では「存在しない」。
- `fatfs_vfs_stat`・`fatfs_vfs_get_size` (と `fatfs_vfs_rmdir` の種別の確認) だけが `ff_stat_to_vfs` を通し、`FR_INVALID_NAME` を `VFS_ERR_NOTFOUND` に写す。他は `ff_to_vfs` のまま (`FR_NO_FILE`・`FR_NO_PATH` は NOTFOUND、`FR_DISK_ERR`・`FR_NOT_READY`・`FR_NO_FILESYSTEM` は `VFS_ERR_IO`。I/O の障害を不存在にしない)。
- open・read・write・stream・unlink・mkdir・rename・list の `FR_INVALID_NAME` は `VFS_ERR_INVAL` のまま (呼び手が不正な名前を渡した、という診断を潰さない)。
- 理由: KAPI の `db_open_existing` は hot journal の有無を `vfs_stat("<db>-journal")` で見て NOTFOUND 以外を IOERR と断じる。INVAL のままだと FDD ブート (root = FAT) で `/etc/settings.db` を開けない。
- stat の成功時は `st_size` と `st_mode` (通常 `0100644`・`AM_DIR` は `0040755`・`AM_RDO` は `0100444`) を埋める。`buf == NULL` は `VFS_ERR_INVAL`。

## 列挙の誤りの伝播 (`fatfs_vfs_list`)

> 出所: TASK_S3I2 (S3I2-K) の契約を今のソースと照らして要約

- 列挙の途中で `f_readdir` が失敗したら、そこで打ち切って `ff_to_vfs` の **負** を返す。それまでに callback へ渡した項目は取り消さない (呼び手は負を見て全体を捨てる)。終端 (`fno.fname[0] == '\0'`) は成功で、空のディレクトリは 0 件で `VFS_OK`。
- `f_closedir` は成功でも打ち切りでも 1 回呼ぶ。`f_opendir` の失敗は負を返し、開けていない物を閉じない。
- `vfs_ls` は `list_dir` の戻りをそのまま返すので、`sys_ls` の呼び手 (`install` の `copy_directory` など) に負が届く。

## 検査

`vfs-mount-dev-host` の中の `tools/tests/test_fatfs_stat.py` (本体 `tools/tests/fatfs_stat_host.c`)。実物の `sys/fs/fat/fatfs_vfs.c` を `#include` し、FatFs の関数だけを贋物にする (`f_stat` は次の `FRESULT` を返す、`f_readdir` は台本を 1 段ずつ返す)。stat の写像 7 件と列挙 4 件。実 FatFs が 8.3 に収まらない名前で `FR_INVALID_NAME` を返すことは贋物で仮定している。
