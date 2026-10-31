# fs/ext2/ext2_vfs_remove_path.inc — unlink/rmdir の親解決

> 状態: **現行 (2026-11-01)**

`ext2_vfs.c` の unlink/rmdir 入口から、`Ext2Ctx *` とパス、
`int (*)(Ext2Ctx *, u32, const char *)` 型の最終操作を受け取る。
パス分離→親の解決→最終操作→ext2/VFS エラー変換の既存順序を保つ。
親解決の失敗は VFS の値をそのまま返し、最終操作を呼ばない。

名前の扱いと検査は既存の `ext2_unlink` / `ext2_rmdir` に残す。
[D1/D6 の保持](ext2_priv.md) と名前空間の失効は既存の下位処理が担当し、
共用部は追加の I/O・割当・失効を行わない。単一タスク・同期操作の前提を継承する。
mkdir/create_excl/rename とディレクトリ一覧の callback は従来の入口に残る。

既存の `test_ext2_write_io.py`、`test_b8_open.py`、`test_ext2_empty_name.py`、
`test_vfs_fd_path.py` の実 include で削除・失敗・再利用を検証する。
callback の字面による抽出は `test_kcallback.py` が検証する。
