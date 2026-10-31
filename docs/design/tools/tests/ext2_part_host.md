# tools/tests/ext2_part_host.c — ext2 区画境界の試験

> 状態: **現行 (2026-10-31)**

区画表・format・mount の幾何と範囲、失敗時の書込み禁止を RAM 媒体で検査する。
媒体の直書きは区画表や初期化・mount 前に限る。LBA 1 の故障は生の区画表読取りを
対象とし、読取保持に隠れない。find_partition／format／mount の 3 入口すべてで
仕掛けが発火したことを g_fail_read_hits で確認する。
format の全セクタ位置の失敗注入と公開順序の検証は維持する。
