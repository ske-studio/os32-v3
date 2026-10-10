CHECK_PAR_ORDER += 143:check-h3b-fixtures-host
check-h3b-fixtures-host:
	python3 -B tools/tests/test_h3b_fixtures.py $(MUT)
	$(call host32_check,test_shutdown_probe.py)

.PHONY: check-h3b-fixtures-host
