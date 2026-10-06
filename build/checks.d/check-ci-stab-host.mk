CHECK_PAR_ORDER += 133:check-ci-stab-host

check-ci-stab-host:
	python3 -B tools/tests/test_ci_stab.py

.PHONY: check-ci-stab-host
