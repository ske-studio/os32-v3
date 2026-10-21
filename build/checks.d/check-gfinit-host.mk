CHECK_PAR_ORDER += 196:check-gfinit-host
check-gfinit-host:
	python3 -B tools/tests/test_gfinit.py $(MUT)
.PHONY: check-gfinit-host
