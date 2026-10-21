CHECK_PAR_ORDER += 196:check-gfinit-exec-host
check-gfinit-exec-host:
	python3 -B tools/tests/test_gfinit_exec.py $(MUT)
.PHONY: check-gfinit-exec-host
