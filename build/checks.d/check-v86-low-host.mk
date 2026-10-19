CHECK_PAR_ORDER += 090:check-v86-low-host
check-v86-low-host:
	python3 -B tools/tests/test_v86_low.py $(MUT)

.PHONY: check-v86-low-host
