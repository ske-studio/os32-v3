CHECK_PAR_ORDER += 210:check-preflight-host
check-preflight-host:
	python3 -B tools/tests/test_preflight.py

.PHONY: check-preflight-host
