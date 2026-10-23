CHECK_PAR_ORDER += 198:check-recover-wire-host
check-recover-wire-host:
	python3 -B tools/tests/test_recover_wire.py $(MUT)
.PHONY: check-recover-wire-host
