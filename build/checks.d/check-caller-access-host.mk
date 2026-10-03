CHECK_PAR_ORDER += 110:check-caller-access-host
# T2d d1: shared identity and actual syscall entry / normal return.
check-caller-access-host:
	python3 -B tools/tests/test_caller_access.py $(MUT)

.PHONY: check-caller-access-host
