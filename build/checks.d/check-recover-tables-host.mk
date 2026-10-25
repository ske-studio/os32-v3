CHECK_PAR_ORDER += 195:check-recover-tables-host
check-recover-tables-host:
	@mkdir -p $(BUILD_OUT)
	python3 -B tools/tests/test_recover_tables.py --extract $(BUILD_OUT)/recover_tables_oracle.json
	python3 -B tools/gen_recover_tables.py --check --input $(BUILD_OUT)/recover_tables_oracle.json
.PHONY: check-recover-tables-host
