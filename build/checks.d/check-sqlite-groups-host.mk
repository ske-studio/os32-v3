CHECK_PAR_ORDER += 058:check-sqlite-groups-host
check-sqlite-groups-host:
	python3 tools/tests/test_sqlite_groups.py

.PHONY: check-sqlite-groups-host
