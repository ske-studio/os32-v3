CHECK_PAR_ORDER += 090:check-v86-gcap-abort-host
check-v86-gcap-abort-host:
	python3 -B tools/tests/test_v86_gcap_abort.py $(MUT)

.PHONY: check-v86-gcap-abort-host
