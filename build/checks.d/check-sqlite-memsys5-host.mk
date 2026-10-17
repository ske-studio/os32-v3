CHECK_PAR_ORDER += 059:check-sqlite-memsys5-host
# T4a-3: host-only allocator/tag experiment, bundled SQLite in ILP32.
check-sqlite-memsys5-host:
	$(call host32_check,test_sqlite_memsys5.py)

.PHONY: check-sqlite-memsys5-host
