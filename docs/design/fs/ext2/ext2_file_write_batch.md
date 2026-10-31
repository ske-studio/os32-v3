# fs/ext2/ext2_file_write_batch.inc — 同期書込みの64ブロック窓

> 状態: **現行 (2026-10-31)**

1回のwriteを `(ctx, ino)` が所有する固定作業領域で処理する。
[bitmap](ext2_inode_write_bitmap.md)、[間接表](ext2_inode_write_bmap.md)、
[生inode表](ext2_inode_write_cache.md) をまとめる。beginでD1全失効、
begin/finish/abortでD6保持を破棄する。D1 resetはD6を変更しない。
単一タスク・非再入のscratch契約を継承し、動的割当とdataの大きなコピーを作らない。

reserveは最大64件を昇順で記録し、data_done(n)は要求全体の成功後だけ進める。
段1ではdataを1本ずつ発行し、全1KiBは呼出元buf、端数だけg_datを使う。
新規端数は0埋め、既存端数はdata RMW。窓境界でもclean表/bitmap/rawを保持する。

公開はdata成功→bitmap→下位表→上位表→inode。streamは失敗したdataの予約を
RAMで取り消して成功prefixを公開する。metadata IOはfs_errorに従い即abort、
inode/sync失敗は過去の窓が成功していてもIO。短い正値はinodeとsyncの成功後だけ返す。

createとtruncate後の全置換はdefer_inodeを立て、最後のfinishまでinodeを公開しない。
窓ごとに確定した新規blockは未公開inodeの写しからだけ辿れる。data/NOSPC失敗では
当窓全体をtrimし、abortしてから既存releaseへ渡す。metadata失敗後は後始末も書かない。
createは子inode完成・finish後に既存add_entry→sync。名前公開失敗時の非解放契約を維持する。

## 落とし穴

abortはI/Oをしない。部分公開のbitmap/表を逆書込みせず、漏れ・孤児・集計差は
ホストe2fsckへ渡す。完了済み512Bが保持される模型を対象とし、装置の電源断キャッシュ
並替えやセクタ破断は保証しない。全Device要求の各512B位置の故障と停止像、
64KiBの6KiB読/69KiB書は `tools/tests/test_ext2_write_batch.py` で検証する。
