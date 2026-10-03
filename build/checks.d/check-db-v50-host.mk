CHECK_PAR_ORDER += 077:check-db-v50-host
# KAPI v50 (db_open_existing / prepare_only / bind_* / error_code、票 S0-K)。実 SQLite + 実 VFS + RAM backend。
check-db-v50-host:
	python3 -B tools/tests/test_kapi_db_v50.py

.PHONY: check-db-v50-host
