CHECK_PAR_ORDER += 148:check-fm-module-host

check-fm-module-host:
	python3 -B tools/tests/test_fm_module.py $(MUT)

.PHONY: check-fm-module-host
