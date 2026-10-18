CHECK_PAR_ORDER += 149:check-v86-module-host

check-v86-module-host:
	python3 -B tools/tests/test_v86_module.py $(MUT)

.PHONY: check-v86-module-host
