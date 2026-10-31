# fs/ext2/ext2_inode_write_cache.inc — 生 inode 表の保持

> 状態: **現行 (2026-10-31)**

[書込み窓](ext2_file_write_batch.md) の開始時に生のinode表を1度読み、対象inodeの
デコード値と窓開始値を保持する。create/全置換はinitialを採用する。
既存のread_inode/write_inodeとdecode/encodeを共用し、保存する欄を揃える。

commit は保持するrawの対象位置だけをencodeして書く。隣接inodeと未知のbyteは
保持し、書込み成功後だけdirtyを落とす。D1失効はこのrawを捨てない。

## 落とし穴

親ディレクトリのRMWをこの子inodeの保持中に挟まない。createはfinishで保持を
閉じてからadd_entryを呼ぶ。隣接inode・uid/gid/generationの保存とinode公開失敗は
`tools/tests/test_ext2_write_batch.py` で検証する。
