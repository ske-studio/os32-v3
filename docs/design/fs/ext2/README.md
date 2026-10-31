# fs/ext2/ — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_A_DESIGN・SLIM_KERNEL_DESIGN (D1 で移した)
ソース: `fs/ext2/`

### ext2_fmt

`static int fmt_write_super(Ext2Ctx *ctx, const Ext2Layout *lay)`
必要なlayout計算値はlayから現行式のまま参照、値の導出時点は移さない
`static int fmt_write_root(Ext2Ctx *ctx, u32 gdt_blocks, u32 inode_tbl_blocks_per_group)`
fmt_ctxは元関数のstaticのままポインタ渡し。ext2_g_blkも元の共用バッファ、スタックへ複製しない

- ext2の新補助が返すのは既存のI/O結果。呼元は元と同じ位置で元と同じエラーを返す。書込み回数・順序を固定する。

`ext2_layout_plan` の計算位置は維持し、`fmt_write_super` は `ext2_write_block` の結果をそのまま返し、呼元の同じ位置で `EXT2_ERR_IO` に変換する。

- root 専用の作業変数と `ret` は `static inline int fmt_write_root(Ext2Ctx *ctx, u32 gdt_blocks, u32 inode_tbl_blocks_per_group)` のローカルとし、既存の I/O ごとの `EXT2_ERR_IO` と成功時 `EXT2_OK` を返す。呼元の元ブロック位置で非ゼロを `EXT2_ERR_IO` にし、`fmt_ctx`・`ext2_g_blk` の所有と計算・読書き順序を維持する。

## 索引

- [ext2_dir.md](ext2_dir.md)
- [ext2_file.md](ext2_file.md)
- [ext2_inode.md](ext2_inode.md)
- [fs/ext2/ext2_super.c](ext2_super.md)

- [ext2_super_block_io.md](ext2_super_block_io.md)

- [ext2_inode_cache](ext2_inode_cache.md)
- [ext2_inode_indirect_cache](ext2_inode_indirect_cache.md)
- [ext2_file_read_cache](ext2_file_read_cache.md)
- [ext2_file_read_run](ext2_file_read_run.md)
- [ext2_priv](ext2_priv.md)
