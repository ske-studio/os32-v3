CHECK_PAR_ORDER += 045:check-db-owned-host
check-db-owned-host:
	MUTATE=$(MUTATE) python3 -B -m unittest discover -s tools/tests -p 'test_kapi_db_owned.py'

.PHONY: check-db-owned-host
