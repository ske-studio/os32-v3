# sys/fs/ext2/ext2_dir.c — ext2 ディレクトリ操作

> 状態: **現行 (2026-10-31)**

ディレクトリの列挙、名前の検査、項目の作成と削除を行う。
rename の更新順序も管理する。

## 落とし穴

- **列挙中の再入**: callback から FS に書くと共有 scratch が変わり、後続の一覧が壊れる。
  走査ブロックを私有バッファに読み、そこから callback を呼ぶ (`sys/fs/ext2/ext2_dir.c` の `ext2_list_dir()`, `sys/fs/ext2/ext2_dir.c` の `ext2_list_dir()`)。
  呼び手も名前を集めてから FS を操作する。FatFs と HostDrv の同型経路は未監査。
  この再入の直接試験は未確認。

- **ディレクトリ rename の順序**: 失敗途中で二つの名前が同じディレクトリを指すと、片方の削除で残りが解放済み inode を指しうる。
  ディレクトリは旧名を先に外し、新名を付ける (`sys/fs/ext2/ext2_dir.c`)。
  I/O 失敗を不存在に読み替えない。失敗経路の回帰は `test_b8_open.py (旧版 6455da9bbf55)`。

- **不正な名前**: 空名は列挙時に読み飛ばされるため、OS32 で読めても媒体が正しいとは限らない (`sys/fs/ext2/ext2_dir.c`)。
  空・`.`・`..`・長すぎる名前・`/` を含む名前は書込み前に拒否する (`sys/fs/ext2/ext2_dir.c` の `ext2_name_check()`)。
  `make check-ext2-empty-name-host` と `test_b8_open.py (旧版 6455da9bbf55)` はホスト像を検査する。
  実インストール像も別途 `e2fsck -fn` で検査する。

## 名前の検査 — 長さ 0 の名前を媒体に載せない

> 出所: TASK_EXT2_EMPTY_NAME の契約を今のソースと照らして要約 (2026-11-18)

ソース: `sys/fs/ext2/ext2_dir.c` (`ext2_name_check`・`ext2_find_entry_loc`・`ext2_add_entry`)・`sys/fs/ext2/ext2_file.c`・`sys/fs/ext2/ext2_dir_directory.inc`・`sys/fs/ext2/ext2_dir_rename.inc`・`sys/fs/ext2/ext2_vfs.c` (`ext2_path_is_root`・`ext2_vfs_mkdir`)。

- `ext2_name_check` は 1 要素の名前を検査する: NULL・空・`.`・`..`・`/` を含む・`EXT2_NAME_LEN` (255) 超は `EXT2_ERR_INVAL`。255 文字はちょうど通る (dirent の `name_len` は u8 なので 256 文字は 0 に回り込み、名前の無い項目になる)。
- 書く入口は全部、媒体に触る前にこれを通す: `ext2_mkdir`・`ext2_create`・`ext2_add_entry`・`ext2_rename` (元と先の両方)・`ext2_rmdir`・`ext2_unlink`。拒否したら 1 セクタも書かない。
- 引く側: `ext2_find_entry_loc` は長さ 0 か 255 超の名前を探さず `EXT2_ERR_NOTFOUND` を返す。既に名前の無い項目を持つ媒体でも `""` で当たらない。
- `ext2_vfs_mkdir` はパスが FS のルートそのもの (`""`・`"/"`・`"//"` …、`ext2_path_is_root`) なら `VFS_ERR_EXIST` (POSIX の `mkdir("/")` と同じ)。親 `/` + 名前 `""` に分けて名前の無いディレクトリを作らない。
- 列挙 (`ext2_list_dir`) は `name_len` 0 の項目を読み飛ばすので、OS32 から一覧しても名前の無い項目は見えない。媒体の正しさは `e2fsck -fn` で確かめる。

検査: `make check-ext2-empty-name-host` (`test_ext2_empty_name.py (旧版 1972f33be05e)`、本体 `ext2_empty_name_host.c (旧版 4034cebef360)`、実物の `fs/ext2/*.c`・`sys/fs/vfs.c`・`sys/fs/vfs_fd.c`・`userland/lib/rt/pkg.c`)。段の終わりに全ディレクトリの生の項目を歩き、像を本物の `e2fsck -fn` に当てる (終了コード 0 以外は失敗)。
