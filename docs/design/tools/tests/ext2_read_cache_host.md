# tools/tests/ext2_read_cache_host.c — 読取回帰試験

> 状態: **現行 (2026-10-31)**

製品の ext2_super/inode/dir/file/fmt/vfs.c をそのまま組む ILP32 RAM 媒体試験。
境界は Device、IDE 存在／幾何、kernel heap／文字列／表示／VFS 登録だけを差し替える。
要求ごとに Device、LBA、count、成否、読書きを記録し、Device 要求数と転送セクタ数を別に数える。
媒体は実 format／mount で作り、読取測定用 inode・表・データだけを準備する。

| 冷読取（inode を含む） | 基点の要求／セクタ | D1 の要求／セクタ |
|---|---|---|
| 128B×8 | 16／32 | 2／4 |
| 単間接 256 ブロック | 513／1026 | 10／516 |
| 二重間接 256 ブロック | 769／1538 | 11／518 |
| 33 連続ブロック | 55／110 | 4／70 |

全バイト・返却長・前後番兵も照合する。12／268／下位表切替、ctx 交替、
inode 値コピーと隣 inode の保持、scratch 上書き、全ブロック直接転送・非登録、
端数枠の維持、穴・不連続・EOF・size=0・非通常ファイルを通す。

全書込み境界で発行前の全枠失効も検査する。block の第 1／第 2 セクタ、sector、
範囲書込みの成功／部分失敗、inode／上位／下位／端数データ／範囲／sector 読取の故障を注入し、
仕掛けの発火と再 I/O を要求する。metadata/data の fs_error 差、エラー中の読み、
mount 早期失敗、unmount の sync 成否、同一 ctx アドレスでの媒体交換も通す。
書込み・追記・mtime・表更新・truncate・unlink→再利用・rename は手動 reset を挟まず検証する。
保持領域は sizeof で 3248B と検査する。

既存足場の媒体直書きも照合した。ext2_part の直書きは区画表・format／mount 前で、
故障は生の LBA 1 を対象とするため保持に隠れない。ext2_write_io は初期化以外に
媒体の直書きや故障切替がなく、fep_standard も format 前の区画表以外は製品 I/O を通す。
両者に操作中の手動 reset は不要。B8 の同一 open 内の 2 回目だけは
[試験限定の抑止口](../../fs/ext2/ext2_inode_cache.md) を使う。

実行入口・変異の契約は [test_ext2_read_cache](test_ext2_read_cache.md)。
NP21/W・実機の内容／時間の受入は PM が別に行う。ホストの回数を実機時間へ換算しない。
