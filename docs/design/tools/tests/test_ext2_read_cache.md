# tools/tests/test_ext2_read_cache.py — 読取回帰試験

> 状態: **現行 (2026-10-31)**

`RAM fixture`（ext2_read_cache_host.md） を GNU11・ILP32・-Werror でビルドし、
`host32` の指定 runner で 12 case を実行する。実デバイスや既存ディスク像は使わない。
`CHECK` に製品 .c/.inc・ヘッダ・fixture・host32・区画表境界を宣言する。

`--mutate` は製品 fs の未変更コピーで全 case が成功した後、18 変異を別々に組む。
inode／表／データの key・hit・値コピー、連続区間と 32 制限、書込発行前・
読取失敗・mount／unmount の失効を壊し、実行時 assertion の rc=1 を必須とする。
コンパイル失敗や runner の異常終了を kill に数えない。
実行は `python3 -B tools/tests/test_ext2_read_cache.py [--mutate]`。
