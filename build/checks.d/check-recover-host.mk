CHECK_PAR_ORDER += 193:check-recover-host
# Pure T1 contracts; no kdebug/kernel build wiring or hardware imports.
check-recover-host:
	python3 -B tools/tests/test_recover.py $(MUT)

.PHONY: check-recover-host
