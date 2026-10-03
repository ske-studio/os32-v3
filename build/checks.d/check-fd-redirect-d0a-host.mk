CHECK_PAR_ORDER += 109:check-fd-redirect-d0a-host
# T2d d0b: registered-AS copy is a passing regression, including runtime mutants.
check-fd-redirect-d0a-host:
	python3 -B tools/tests/test_fd_redirect_d0a.py $(MUT)

.PHONY: check-fd-redirect-d0a-host
