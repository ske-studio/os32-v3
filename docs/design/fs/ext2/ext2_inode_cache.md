# fs/ext2/ext2_inode_cache.inc — 読取保持

> 状態: **現行 (2026-10-31)**

inode の read 入口だけで `(ctx, ino)` を照合する固定 1 枠。デコード成功後の
`Ext2Inode` を値で保存し、hit でも値コピーを返す。write の read-modify-write は
生の inode 表を読むため、同じ表の隣の inode を残す。
miss 開始時に valid を落とす。mounted かつ fs_error なしでのみ照合・登録する。
キーの ctx は識別だけに使い、保持ポインタから参照しない。

`EXT2_READ_CACHE_HOST_TEST` 時だけ `ext2_inode_cache_test_disable()` を持つ。
B8 の stat→サイズ取得の 2 回目の inode I/O 故障を保つため、対象 case の入口・出口で
全失効し、その間だけ inode の照合・登録を抑止する。製品には含まない。
共通失効は [ext2_priv](ext2_priv.md)、試験は `RAM fixture`（../../tools/tests/ext2_read_cache_host.md）。
