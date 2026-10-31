# tools/tests/ext2_read_bound_host.c — 読取回帰試験

> 状態: **現行 (2026-10-31)**

製品 ext2_file.c の範囲処理を取り込み、inode／bmap／データ I/O の境界を偽物にする。
端数・exec のヘッダ・前後番兵・stream の種別／EOF／穴／IO の戻り値を検査する。
連続全ブロックの範囲 I/O も偽媒体の各ブロックへ展開する。

偽媒体内容と故障条件の切替は ctx_reset に集約し、端数枠もそこで失効する。
成功した読取と穴までの短い読取では内容と番兵を照合する。失敗時の宛先内容は
成功した prefix として扱わず、負の IO を必須とする。bmap の先行調査で失敗した場合は
データ I/O より前に戻り得る。
入口は [test_ext2_read_bound](test_ext2_read_bound.md)。
