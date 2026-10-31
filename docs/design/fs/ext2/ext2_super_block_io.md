# fs/ext2/ext2_super_block_io.inc — 生の範囲 I/O

> 状態: **現行 (2026-10-31)**。D1 保持枠との統合は計画。

`ext2_super.c` 内のブロック／セクタ I/O を持つ。
`ext2_read_data_blocks(ctx, first, blocks, dst)` と
`ext2_write_data_blocks(ctx, first, blocks, src)` は区画内の物理連続範囲を受ける。
blocks は 1〜`EXT2_DATA_RUN_MAX_BLOCKS` (32)、宛先／入力は blocks×1024B。
全範囲を検査し、有効なら Device へ count=blocks×2 の要求を 1 回だけ出す。

検査は乗算前に `limit=part_len/2`、`first<limit`、`blocks<=limit-first` を確認し、
base と開始・末尾 LBA の 32bit overflow を拒否する。
不正な範囲は I/O なしで `EXT2_ERR_IO`。有効範囲の成功は 0、
Device の失敗は負のエラーをそのまま返す。READ 失敗時の dst は未定義、
WRITE 失敗時は一部変更済みになり得る。data 用の口は `ext2_fs_error` を呼ばない。

`ext2_read_block` と `ext2_read_data_block` は同じ生読みへ blocks=1 で委譲する。
metadata の口だけ失敗時に `ext2_fs_error` を呼ぶ。
既存の raw 単ブロック書込みはセクタ 0 → 1 の独立した要求順序を維持する。
sector の公開操作も count=1 のまま。mount 途中でも使うため mounted は要求しない。
保持・先読み・登録・32KiB バッファの確保は行わず、呼び手の宛先へ直接転送する。

## D1 への受渡し

D1 の `ext2_file_read_run.inc` が同じ ctx 内の物理連続全 1KiB 区間を探し、
最大 32 ブロックごとに範囲読みを呼ぶ。全区間は保持枠を迂回し、登録もしない。
EOF・穴・端数・不連続を越えないループは D1 が担当する。

D2 先行の本基点には D1 保持枠・失効フックが存在しないため、偽の失効処理は置かない。
D1 統合時に、`ext2_raw_write_block`、`ext2_write_sector`、`ext2_write_data_blocks`
から Device 書込みを出す前と、`ext2_raw_read_blocks`・`ext2_read_sector` の生読み失敗時に
D1 所有の失効フックを結線する。mount/unmount もその所有者と統合する。
`ext2_fs_error` の superblock 書込みは失効対象外。
範囲 I/O の関数別試験は [ext2_block_io_host](../../tools/tests/ext2_block_io_host.md)。

## 落とし穴

既存 metadata の書込みを範囲 API に置換すると、障害時の公開境界が変わる。
本段では呼び手側の書込みを束ねるループを追加しない。
