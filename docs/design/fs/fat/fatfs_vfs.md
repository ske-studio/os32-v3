# fs/fat/fatfs_vfs.c — FatFs の VFS 接続

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
  現行の列挙は FatFs が返す `fno.fname` を VFS 名へ写す (`fs/fat/fatfs_vfs.c:197`)。
  過去の固定長構造を現行 FatFs の契約とみなさない。直接の配列境界回帰は未確認。
