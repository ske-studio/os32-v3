# fs/ext2/ext2_priv.h — 読取保持

> 状態: **現行 (2026-10-31)**

ext2 内部の宣言と範囲 I/O の最大 32 ブロック定数を共有する。
読取保持は全 mount 共用の inode 1 件・間接表 2 枚・端数データ 1 枚。
単一利用者・単一タスク・協調切替と既存同期 I/O の非再入前提を継承する。

`ext2_read_cache_reset()` は 4 枠の valid だけを落とし、I/O・割当・ゼロ消去をしない。
`ext2_inode`（ext2_inode.md） が inode／表の失効と `ext2_data_cache_reset()` を束ねる。
全枠の照合・登録は mounted かつ fs_error なしの場合に限る。

- [生 I/O](ext2_super_block_io.md) の全書込みは Device 発行前に全失効する。
  第 2 セクタだけ失敗しても、旧枠を hit しない。inode、表、データ、日時、
  truncate、unlink／再利用、rename、format の変更を同じ口で扱う。
- 生の読取失敗（範囲／sector を含む）と `ext2_fs_error()` 入口も全失効する。
  エラー通知内の superblock 書込みには追加のフックを置かない。
- mount 入口は早期失敗より前、unmount は sync 後かつ ctx 解放より前に全失効する。
  sync 失敗でも行う。ctx の同一アドレス再利用へ持ち越さない。

mount 中の無通知媒体交換や ext2 外の直書きは検知しない。
媒体交換は unmount→交換→mount の運用に従う。

保持内容は 3×1024＋128=3200B、キーと valid は 4×12=48B。
i386 の保持オブジェクトの合計は 3248B（host ILP32 sizeof と kernel.elf の nm -S で検証）。
実ビルドの map では inode.o の .bss が 2252B、file.o が 1036B で合計 3288B。
オブジェクト間のアラインメント 40B を含み、基点の両 .bss はともに 0B。
ヒープ・mount／FD 数に応じた増加・1KiB のスタック配列はない。
