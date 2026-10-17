CHECK_PAR_ORDER += 146:check-x11-shared-host
check-x11-shared-host:
	python3 -B tools/tests/test_x11_shared.py $(MUT)

.PHONY: check-x11-shared-host
