# fs/ext2/ext2_file_read_cache.inc — 読取保持

> 状態: **現行 (2026-10-31)**

端数データ専用の固定 1 枠。キーは `(ctx, physical_block)`、内容は専用の 1KiB 配列。
miss 開始時に valid を落として `ext2_read_data_block()` で全体を読み、成功後だけ
mounted かつ fs_error なしなら登録する。要求部分だけを宛先へコピーする。
データ障害は fs_error を立てず、読取失敗は生 I/O 側で全枠を失効する。

共有 scratch への書込みで内容は壊れない。別の全 1KiB 区間を読む場合もこの枠を
照合・登録・追出ししない。別の端数ブロックへの miss だけが置換する。
`範囲読取`（ext2_file_read_run.md） と [共通失効](ext2_priv.md) を参照。
