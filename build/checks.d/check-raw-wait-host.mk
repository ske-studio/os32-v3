CHECK_PAR_ORDER += 177:check-raw-wait-host
check-raw-wait-host:
	python3 -B tools/tests/test_raw_wait.py $(MUT)
.PHONY: check-raw-wait-host
