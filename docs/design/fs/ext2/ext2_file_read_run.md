# fs/ext2/ext2_file_read_run.inc — 読取保持

> 状態: **現行 (2026-10-31)**

`ext2_read_file()` と `ext2_read_stream()` の共通の範囲読取を持つ。
inode.size と要求サイズで remaining を制限し、bmap が返す物理番号が 1 ずつ増える
全 1KiB 区間を作る。最大 `EXT2_DATA_RUN_MAX_BLOCKS` (32) ごとに
`ext2_read_data_blocks()` を 1 回呼び、宛先へ直接転送する。
EOF・穴・端数・物理不連続を越えない。33 以上の連続ブロックは複数要求に分かれる。

端数は [ext2_file_read_cache](ext2_file_read_cache.md) で処理する。
全 1KiB 読取は端数枠を照合・登録・置換しない。穴と未対応の三重間接は既読長、
途中の bmap／データ読取失敗は負の IO を返す。連続区間の調査で次の表の故障が
先に判明する場合もあり、失敗時の宛先内容を成功した部分読取として扱わない。
[回数・境界・失効の試験](../../tools/tests/ext2_read_cache_host.md) を参照。
