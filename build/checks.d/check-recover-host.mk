CHECK_PAR_ORDER += 193:check-recover-host
# T1/T1b/T2 contracts, injectable window/display/PIT bus; no command wiring.
check-recover-host:
	python3 -B tools/tests/test_recover.py $(MUT)

.PHONY: check-recover-host
