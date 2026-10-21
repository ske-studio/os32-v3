CHECK_PAR_ORDER += 209:check-gfinit-plan-host
check-gfinit-plan-host:
	python3 -B tools/tests/test_gfinit_plan.py $(MUT)
.PHONY: check-gfinit-plan-host
