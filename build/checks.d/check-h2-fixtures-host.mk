CHECK_PAR_ORDER += 130:check-h2-fixtures-host
check-h2-fixtures-host:
	python3 -B tools/tests/test_h2_fixtures.py $(MUT)

.PHONY: check-h2-fixtures-host
