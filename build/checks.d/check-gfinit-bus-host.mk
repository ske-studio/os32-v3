CHECK_PAR_ORDER += 198:check-gfinit-bus-host
check-gfinit-bus-host:
	python3 -B tools/tests/test_gfinit_bus.py $(MUT)
.PHONY: check-gfinit-bus-host
