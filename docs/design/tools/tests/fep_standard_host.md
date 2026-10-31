# tools/tests/fep_standard_host.c — IME・SQLite・ext2 の統合回帰

> 状態: **現行 (2026-10-31)**

製品 IME、SQLite/MEMSYS5 の実設定、VFS/FD、ext2 を ILP32 の RAM 媒体へ接続する。
境界は Device／IDE、owner／TTY、kernel heap。実デバイスを使わず、
SQLite の製品 512KiB pool と FEP の割当て契約を検証する。入口は `test_fep_standard.py`。

媒体の直書きは setup の区画表作成だけで、その後は実物の format／mount と製品 I/O を通す。
読取失敗の注入や媒体内容の外部切替はない。D1 の読取保持が SQLite の既存 I/O を
透過的に扱うことを確認し、製品書込みの区間へ手動 reset を挟まない。
D1 の故障・保持回数は [専用 fixture](ext2_read_cache_host.md) が検証する。
