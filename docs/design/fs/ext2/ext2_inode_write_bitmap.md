# fs/ext2/ext2_inode_write_bitmap.inc — 書込み中の bitmap 予約

> 状態: **現行 (2026-10-31)**

[書込み窓](ext2_file_write_batch.md) の所有者だけが使う。block bitmap は群ごとに最大32枚、
inode bitmap は create の1枚を保持する。既存の群・bitの探索順と最終群の端数を守る。
予約は保持配列の bit と reserved だけを変え、二重予約を防ぐ。ctx の空き数から
未確定の reserved を引いて満杯を判定する。

commit はデータ完了後に各 dirty bitmap を書き、成功した群の差分だけ ctx の
空き数へ反映して meta_touch する。inode bitmap も inode 公開より先に書く。
失敗した群以降へ進まず IO を返し、逆書込みや再利用をしない。
cancel は当窓の未確定 bit だけをRAMで戻し、I/Oを出さない。

## 落とし穴

後始末で媒体の bit を返す前に workspace を abort する。保持 bitmap を残すと
後の finish が返却済みの bit を再度立てる。故障・回数・全32群の回帰は
`tools/tests/test_ext2_write_batch.py`。
