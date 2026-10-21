CHECK_PAR_ORDER += 196:check-rshell-recover-host
check-rshell-recover-host:
	python3 -B tools/tests/test_rshell_recover.py $(MUT)
.PHONY: check-rshell-recover-host
