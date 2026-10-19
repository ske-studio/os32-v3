CHECK_PAR_ORDER += 090:check-v86-call-host
check-v86-call-host:
	python3 -B tools/tests/test_v86_call.py $(MUT)

.PHONY: check-v86-call-host
