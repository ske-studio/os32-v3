CHECK_PAR_ORDER += 219:check-gf-runner-host

check-gf-runner-host:
	python3 tools/run_host_group.py --plain tools/tests/test_gf_runner.py

.PHONY: check-gf-runner-host
