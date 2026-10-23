CHECK_PAR_ORDER += 199:check-recover-t4b-host
check-recover-t4b-host:
	python3 -B tools/tests/test_recover_t4b_host.py $(MUT)
.PHONY: check-recover-t4b-host
