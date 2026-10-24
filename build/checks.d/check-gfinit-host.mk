CHECK_PAR_ORDER += 196:check-gfinit-host
# TT3: keep every script/control/mutation; recursive make shares the jobserver.
check-gfinit-host:
	+python3 -B tools/run_host_group.py $(MUT) \
	    --plain tools/tests/test_gfinit.py \
	    --plain tools/tests/test_gfinit_exec.py \
	    --plain tools/tests/test_gfinit_pll.py \
	    --plain tools/tests/test_gfinit_bus.py \
	    --plain tools/tests/test_gfinit_owner.py \
	    --plain tools/tests/test_gfinit_plan.py

.PHONY: check-gfinit-host
