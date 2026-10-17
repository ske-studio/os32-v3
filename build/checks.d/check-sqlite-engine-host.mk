CHECK_PAR_ORDER += 176:check-sqlite-engine-host
check-sqlite-engine-host:
	$(call host32_check,test_sqlite_engine.py)
.PHONY: check-sqlite-engine-host
