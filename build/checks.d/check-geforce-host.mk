CHECK_PAR_ORDER += 198:check-geforce-host
check-geforce-host:
	python3 -B tools/tests/test_geforce.py
.PHONY: check-geforce-host
