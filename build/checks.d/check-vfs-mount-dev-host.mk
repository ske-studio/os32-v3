CHECK_PAR_ORDER += 057:check-vfs-mount-dev-host
# fs/vfs.c + fs/ext2_vfs.c の mount 経路。fd0 が hd0 に化けて同じ
# パーティションを二重マウントする回帰 (2026-09-10) を止める。
check-vfs-mount-dev-host:
	python3 -B tools/tests/test_vfs_mount_dev.py
	python3 -B tools/tests/test_ext2_read_bound.py
	python3 -B tools/tests/test_ext2_write_io.py
	python3 -B tools/tests/test_fatfs_stat.py

.PHONY: check-vfs-mount-dev-host
