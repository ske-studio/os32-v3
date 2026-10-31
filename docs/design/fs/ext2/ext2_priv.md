# fs/ext2/ext2_priv.h — 内部宣言と読取・書込保持

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

## D6 の共有契約 (段 1-0)

この段は型・定数・extern 宣言だけを追加する。保持領域の実体、関数の実装、
既存 write からの接続は後段で追加し、現時点の実行時の振る舞いは変えない。
D2 の `EXT2_DATA_RUN_MAX_BLOCKS` と範囲 I/O の宣言は変更しない。

`Ext2WriteWork` は同期 write 1 回につき `(ctx, ino)` 1 組が所有する固定領域。
後段の batch 実装が `ext2_write_work` を 1 個定義する。create の inode 予約前は
`ino == EXT2_WRITE_NEW_INO` (0)。D1 とは別配列で、D1 reset は D6 を捨てない。
begin は D1 を全失効し、begin/finish/abort は D6 の全 valid を落とす。
既存 scratch と同じ非再入・操作中の別 ext2 操作への協調切替なしを前提とする。

保持する型はブロック bitmap 32 枚、inode bitmap 1 枚、間接表 3 枚、
生 inode 表 1 枚とデコード値・窓開始値、予約/進捗 67 件、inode 予約 1 件。
表の slot 0 は上位、slot 1〜2 は単間接/下位。bitmap の群は配列添字、
inode bitmap の群は `inode_reservation.group`、表のキーは `physical`。
各 valid は所有者が一致する間だけ有効。生 inode 表の隣接 inode・未知欄を保つ。
製品の ILP32 配置は `STATIC_ASSERT` で記録 16B と作業領域 40KiB 以下を固定する。
既存 native host 模型の LP64 の u32 では、この配置検査を適用しない。

窓は最大 `EXT2_WRITE_WINDOW_BLOCKS` (64) data 件、表予約は最大 3 件。
data 記録は `record[0..data_count)`、表記録は `record[64..64+table_count)`。
記録は logical/physical、親の物理番号、項目 index、種別、状態を持つ。
親 0 は inode.block、他は保持する LE32 表。追加は既存の 0 項目だけに限る。
NEW は当窓の予約、DATA_DONE は要求全体の成功、BITMAP_DONE は媒体の bit 確定、
LINKED は RAM の親への追加を表す。過去の窓で確定した予約は取消対象にしない。

| 担当 | 口と受渡し |
|---|---|
| bitmap | reserve/reserve_inode は現行探索順で RAM だけに予約。cancel は未確定の新規 bit だけを RAM で取消。commit は各 bitmap 書込み成功後だけ空き数を減らし meta_touch |
| bmap | get は未割当 0 と IO を分離。set は必要な全 0 表を予約し追加位置を記録。trim は data_done 以後と不要な新規表を取消。commit は dirty な下位→上位だけ排出 |
| inode | load は生表を一度読み initial があれば create の初期値を採用。commit は対象欄だけ encode |
| batch | begin/map/reserve/data_done/commit_window/finish/abort が上記をまとめる |

begin の initial は既存なら NULL、create なら初期 inode。create は名前の存在確認後、
全置換は既存 truncate 成功後に begin する。reserve は logical 昇順で既存/新規の
data 記録を追加し、そのポインタを返す。map は検索のみで窓へ追加しない。
data_done(n) は成功 prefix を n 件前進させる。失敗した範囲要求は全件未計上。
段 2 はこの記録列を使い、buf の連続性と物理番号 +1 を満たす全 1KiB 部分を
最大 32 件で切る。データの大きなコピーや次窓の先読みは追加しない。

commit_window は未完了 suffix を trim し、data 成功→bitmap→下位表→上位表→
inode の順で公開。clean 枠は次窓へ保持し、窓開始 inode と件数を更新する。
finish は残りの窓/inode を排出して閉じ、従来の sync は呼び手が完了する。
create は finish→既存 add_entry→sync、stream の短い正値は対応する inode と
sync の成功後だけ返す。inode/sync IO では過去の窓が成功していても IO を返す。
metadata IO は既存口の fs_error に従い以後の通常書込みを止め、abort する。
data IO は fs_error を立てず、成功 prefix だけを commit する。

abort は I/O をせず全保持を捨てる。公開不明の bit/参照を逆書込み・再利用しない。
create の data 失敗では既存の ext2_create_abort_unwritten→ext2_release_blocks
より先に abort し、返した bit を保持 bitmap が再度立てる経路を作らない。
