CHECK_PAR_ORDER += 198:check-recover-virtual
check-recover-virtual:
	python3 -B tools/tests/test_recover_virtual.py $(MUT)
.PHONY: check-recover-virtual
