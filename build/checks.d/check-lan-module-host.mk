CHECK_PAR_ORDER += 150:check-lan-module-host

check-lan-module-host:
	python3 -B tools/tests/test_lan_module.py $(MUT)

.PHONY: check-lan-module-host
