CHECK_PAR_ORDER += 211:check-check-times-host
check-check-times-host:
	python3 -B tools/tests/test_check_times.py $(MUT)

.PHONY: check-check-times-host
