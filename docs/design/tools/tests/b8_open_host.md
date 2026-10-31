# tools/tests/b8_open_host.c — 読取回帰試験

> 状態: **現行 (2026-10-31)**

実物の VFS/FD と ext2 を RAM 媒体へ接続し、open の故障・公開と解放の順序を検証する。
故障を仕掛ける fail_arm は準備時に読取枠を全失効し、対象 I/O が hit で隠れないようにする。
製品の書込み失効を確認する途中に手動 reset を挟まない。

同一 open の stat→サイズ取得で 2 回目の inode 読取だけを落とす size_failure と
sqlite_size_failure は `EXT2_READ_CACHE_HOST_TEST` を使い、case の操作前後で
inode 保持を抑止／復帰する。パスメモは温めたまま fail_arm(lba, 2) を維持する。
故障発火・負値・NOTFOUND ではない・FD 未発行・内容保持・状態通知以外の書込みなしを確認する。
抑止口は [inode 保持](../../fs/ext2/ext2_inode_cache.md) を参照。

空ディレクトリの rmdir は、空判定中に単間接表を 1 回だけ読み、参照を外す
書込み後の 2 回目が解放時の生読みとなる。この 2 回目へ故障を仕掛け、
発火・IO・inode 解放・表と配下の漏れを従来どおり検証する。
