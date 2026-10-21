CHECK_PAR_ORDER += 197:check-recover-t3
check-recover-t3:
	python3 -B tools/tests/test_recover_t3.py $(MUT)
.PHONY: check-recover-t3
