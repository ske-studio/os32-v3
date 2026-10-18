CHECK_PAR_ORDER += 143:check-double-fault-host
check-double-fault-host:
	python3 -B tools/tests/test_double_fault.py $(MUT)
.PHONY: check-double-fault-host
