CHECK_PAR_ORDER += 197:check-gfinit-pll-host
check-gfinit-pll-host:
	python3 -B tools/tests/test_gfinit_pll.py $(MUT)
.PHONY: check-gfinit-pll-host
