CHECK_PAR_ORDER += 151:check-cirrus-module-host

check-cirrus-module-host:
	python3 -B tools/tests/test_cirrus_module.py $(MUT)

.PHONY: check-cirrus-module-host
