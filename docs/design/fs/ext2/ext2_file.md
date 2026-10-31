# fs/ext2/ext2_file.c — ext2 ファイル I/O

> 状態: **現行 (2026-10-31)**

inode の範囲を読み、ファイルの書込みや切詰めを行う。
通常の読込みとストリーム読込みは [共通の範囲処理](ext2_file_read_run.md) を使う。
端数は [専用の保持枠](ext2_file_read_cache.md)、連続全ブロックは最大 32 ブロックの範囲 I/O へ渡す。

書込みのstream/create/全置換は [64ブロック窓](ext2_file_write_batch.md) に接続する。

## 落とし穴

- **部分ブロックの読込み**: 端数でも直接ブロックを読むと要求先の後ろまで 1KB 書いて隣を壊す。
  部分ブロックは scratch と独立の専用 1KiB 配列に読み、要求範囲だけ写す。
  `ext2_g_aux` はブロック変換と競合するため中継に使わない。
  小さな宛先と境界の回帰は `tools/tests/test_ext2_read_bound.py`。
