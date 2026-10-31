# tools/tests/ext2_write_io_host.c — ext2 書込み I/O の回帰

> 状態: **現行 (2026-10-31)**

製品 ext2_super/inode/dir/file/fmt/vfs.c と microtar を ILP32 の RAM 媒体へ接続し、
512B セクタの読書き数と Device 読取要求を数える。書込みの順序、名前空間の失効、
sync、metadata の到達を検証する。実行入口は `test_ext2_write_io.py`。

媒体の直接変更は disk_setup の初期化・区画表作成だけで、その後は製品の format／mount を通る。
媒体を直接参照する検査は書き換えず、故障条件の切替もないため、D1 保持導入に伴う
試験中の手動 reset は不要。製品の書込み前失効と透過的な再読をそのまま検証する。
回数を検証する [D1 の統合 fixture](ext2_read_cache_host.md) と併せて実行する。
