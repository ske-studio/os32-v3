CHECK_PAR_ORDER += 046:check-vfs-fd-sqlite-host
check-vfs-fd-sqlite-host:
	python3 tools/tests/test_vfs_fd_sqlite.py

.PHONY: check-vfs-fd-sqlite-host
