CHECK_PAR_ORDER += 092:check-cd-read-host
# CD の読み (drivers/atapi.c の複数セクタの READ(10)、fs/iso9660.c の覚えた
# パス・先読みの窓・ディレクトリの LRU、userland/lib/rt/pkg.c の区切り)。
# 実機 PC-9821Ra266 の cdinst が 20KB/s を切った件 (READ(10) を 1 セクタずつ、
# VFS の 4KB の区切りごとに根のディレクトリを読み直していた)。
# 本物の atapi.c を ATAPI デバイスの模型 (tools/tests/atapi_hostshim/io.h) の
# 上で回す。変異は一時の木の写しに当てるのでソースは書き換えない (check-par)。
# 記録は tools/tests/cd_read_tdd.md。
check-cd-read-host:
	python3 -B tools/tests/test_cd_read.py --target $(MUT)

.PHONY: check-cd-read-host
