CHECK_PAR_ORDER += 058:check-sqlite-vfs-identity-host
check-sqlite-vfs-identity-host:
	python3 tools/tests/test_sqlite_vfs_identity.py

.PHONY: check-sqlite-vfs-identity-host
