CHECK_PAR_ORDER += 148:check-kcg-module-host
# T5c-1: ROM bytes/ports, typed facade lifetime and runtime mutations.
check-kcg-module-host:
	python3 -B tools/tests/test_kcg_module.py $(MUT)

.PHONY: check-kcg-module-host
