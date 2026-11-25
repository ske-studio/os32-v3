# fs/ext2 — 読み取りの失敗を「無い」「未割当」と区別する

> 状態: **現行 (2026-11-18)** — 出所: TASK_FS_TYPE (B8) の契約を今のソースと照らして要約
ソース: `sys/fs/ext2/ext2_inode.c` (`ext2_bmap`)・`sys/fs/ext2/ext2_dir.c` (`ext2_find_entry`・`ext2_lookup`・`ext2_list_dir`・`ext2_add_entry`・`ext2_delete_entry`)・`sys/fs/ext2/ext2_dir_directory.inc` (`ext2_mkdir`・`ext2_rmdir`)・`sys/fs/ext2/ext2_dir_rename.inc` (`ext2_rename`・`ext2_parent_of`・`ext2_is_self_or_descendant`)・`sys/fs/ext2/ext2_file.c` (`ext2_create`・`ext2_get_size_ino`・`ext2_write_stream`)・`sys/fs/ext2/ext2_vfs.c` (`ext2_to_vfs_err`・`ext2_resolve_path`)・`sys/fs/ext2/ext2_super.c` (`ext2_fs_error`・`ext2_check_writable`)

一度だけ読めなかった間接ブロックやディレクトリブロックを「未割当」「名前が無い」と読むと、
その先の段で `NOTFOUND` になり、open の `O_CREAT` が既存のファイルを作り直す・`cat <ディレクトリ>` が通る、などの被害になる。
**各段が「読めなかった」をそのまま上へ返す。** 受け手側 (VFS の open) の約束は `docs/design/fs/vfs_fd_open.md`。

## 下の段: `ext2_bmap`

- 戻り値 `EXT2_OK` + `*out_phys = 0` は**未割当**、`EXT2_ERR_IO` は間接ブロックが読めなかった (`*out_phys = 0`)。
- 番兵値ではなく戻り値と出力引数を分けてある (呼び手全員がどちらかを判断する形)。

ブロック番号を引く呼び手はどれも IO を IO として扱う。

| 呼び手 | 読めなかったとき |
|---|---|
| `ext2_find_entry` | 検索の終わりにしない。`EXT2_ERR_IO` (`NOTFOUND` にしない) |
| `ext2_list_dir` | 打ち切りを成功にしない |
| `ext2_add_entry` | 新しいブロックを継ぎ足さない |
| `ext2_delete_entry` | `NOTFOUND` に化けさせない |
| 読み出し (`ext2_read_file`・ストリーム読み) | 短いファイル・短い読み出しにしない |
| 書き込み (`ext2_write_stream` が使う `ext2_write_bmap_get`。`ext2_bmap` とは別の、書き込みの作業域を引く口) | 間接表を引けなければそのエラーを返す。未割当と見なして割り当て直さない (元のブロックを残す) |
| `ext2_is_dir_empty` | 「空でない」と偽らない (`rmdir` は `NOTEMPTY` ではなくそのエラー) |
| `ext2_parent_of` (rename の循環検査) | 「祖先ではない」にしない (`ext2_is_self_or_descendant` は負値を返し、rename は `INVAL` と偽らずに返す) |

## 名前の検索の受け手

- `ext2_lookup` はパスの各要素の `ext2_find_entry` のエラーをそのまま返す。
- ext2 の VFS 操作 (stat・get_size・read・write・unlink・rename・mkdir・rmdir・set_mtime など) はパスの解決の失敗を一律 `NOTFOUND` にせず、
  `ext2_to_vfs_err` で写す (`EXT2_ERR_IO` → `VFS_ERR_IO`、`ROFS` → `VFS_ERR_ROFS`、`MLINK` → `VFS_ERR_FULL`、知らない値は IO)。
- `ext2_find_entry` の結果で書き込むかどうかを決める箇所は **3 値**で受ける:
  `EXT2_OK` なら「在る」(mkdir・create は `EXIST`、rename の宛先は置き換えの分岐)、`EXT2_ERR_NOTFOUND` のときだけ作成へ進む、
  **それ以外は何も書かずにそのまま返す**。該当: `ext2_mkdir`・`ext2_create`・`ext2_rename` の宛先・一括書き込み (`ext2_vfs_write`) の既存判定。
  rename の宛先を「読めない = 無い」とすると、宛先に同名の項目が二重にでき、移動元の名前が消える。

## サイズ取得はディレクトリに答えない

`ext2_get_size_ino` は inode がディレクトリなら `EXT2_ERR_ISDIR`。ディレクトリの inode にもサイズはあるので、
答えてしまうと open の受け手が「サイズが取れた = 通常ファイル」の根拠にできる。

## 書き込みの最後の確定

- `ext2_write_stream` は `ext2_write_inode` か `ext2_sync` が落ちたら `EXT2_ERR_IO` を返し、部分成功の値を返さない
  (長さは inode にしか無いので、inode を書けなければ「そのバイト数が読み戻せる」と言えない)。同じ offset へ同じ内容を書き直すのは安全。
- `ext2_create`・`ext2_write`・`ext2_mkdir`・`ext2_rename` の最後の `ext2_sync` の失敗も返す (この FS の約束は「戻った時点でディスクが正しい」)。

## メタデータの I/O エラーで書き込みを止める

- メタデータのブロック I/O (superblock・グループ記述子・ビットマップ・inode 表・ディレクトリブロック・間接表) が失敗したら
  `ext2_fs_error` でマウントをエラー状態にし、以後の書き込み系操作は入口の `ext2_check_writable` が `EXT2_ERR_ROFS` で断る
  (Linux ext2 の `errors=remount-ro` に当たる)。読み取りは止めない。媒体の `s_state` にエラーを立てる書き込みを 1 度だけ試みる。
- データブロックの I/O 失敗はエラーを返すだけで、エラー状態にしない。
- 作る像の `s_errors` は RO (`ext2_fmt.c`)。

## ディレクトリの links の守り

- `ext2_rmdir` は空でも `links_count` が 2 (`EXT2_EMPTY_DIR_LINKS`) を超えていれば inode を返さず `NOTEMPTY` で断る
  (名前の無い子の `..` がまだ指している。返すと解放済み inode を指す名前が残る)。エラー状態にはしない。
- 1 つのディレクトリに名前が 2 つある媒体 (旧いコードが書いたもの・破損) への rename は、完了させずにエラー状態にして IO を返す。

## 保証しないこと

- 解放は「参照を外して書いてから返す」順なので、途中の I/O 失敗は**漏れ** (使用中だが未参照のブロック) になる。
  ゲスト内に fsck は無く、ホストの `e2fsck` で回収する。
- 二重故障 (独立した 2 か所の I/O 失敗) は一部の経路の組み合わせしか掃いていない。
