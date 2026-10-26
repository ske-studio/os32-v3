CHECK_PAR_ORDER += 088:check-tick-host
# Real resident IRQ0 table; CPU IF is supplied by the host harness.
check-tick-host:
	python3 -B tools/tests/test_tick.py $(MUT)

.PHONY: check-tick-host
