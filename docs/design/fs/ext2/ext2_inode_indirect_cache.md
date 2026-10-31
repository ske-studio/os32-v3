# fs/ext2/ext2_inode_indirect_cache.inc — 読取保持

> 状態: **現行 (2026-10-31)**

二重間接の上位表を 1 枠、単間接／二重間接の下位表を共用の 1 枠へ保持する。
キーは `(ctx, physical_block)`。mounted かつ fs_error なしでだけ有効とし、
各枠は miss 開始時に valid を落とし、全 1KiB 読取成功後だけ登録する。
`ext2_read_block()` を通すため、表の読取失敗は従来どおり metadata 障害となる。

返すのは `le32_rd()` で取り出した 1 項目の値であり、表のポインタは貸さない。
上位の値を局所変数へ移してから下位を読む。未割当 0 の表は bmap 側で打ち切る。
scratch の上書きや下位表の切替で上位を破壊しない。
共通失効は [ext2_priv](ext2_priv.md)、bmap の結線は `ext2_inode`（ext2_inode.md）。
