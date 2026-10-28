CHECK_PAR_ORDER += 196:check-gfinit-mode-host
check-gfinit-mode-host:
	python3 -B tools/tests/test_gfinit_mode.py $(MUT)
.PHONY: check-gfinit-mode-host
