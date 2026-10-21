CHECK_PAR_ORDER += 195:check-recover-gf
check-recover-gf:
	python3 -B tools/tests/test_recover_gf.py $(MUT)
.PHONY: check-recover-gf
