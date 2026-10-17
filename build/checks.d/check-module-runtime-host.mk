CHECK_PAR_ORDER += 146:check-module-runtime-host
# 記録: tools/tests/module_runtime_tdd.md
check-module-runtime-host:
	python3 -B tools/tests/test_module_runtime.py $(MUT)

.PHONY: check-module-runtime-host
