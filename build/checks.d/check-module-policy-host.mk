CHECK_PAR_ORDER += 147:check-module-policy-host
check-module-policy-host:
	python3 -B tools/tests/test_module_policy.py $(MUT)

.PHONY: check-module-policy-host
