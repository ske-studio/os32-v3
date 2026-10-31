# cd_read_host.c — CD 読取りの共有 fixture

> 状態: **現行 (2026-10-31)**

製品 atapi.c / iso9660.c / cd_body.c を include し、dev.c / vfs.c と組む。
合成 ISO、レジスタ模型、Device 要求列、READ(10) 列、入出力語列を共有する。
新しいケースの本体は [ディレクトリ試験](cd_read_host_dir.md) と
[PIO 試験](cd_read_host_pio.md) に置き、本体には include と dispatch を置く。

既存の 4KiB/32KiB/奇数位置のファイル全バイト照合、交互パスの `dir_lru`、
窓なしの再入、UA と待ち・reset の回帰も同じ製品コードを通す。
実行入口は [test_cd_read.py](test_cd_read.md)。
