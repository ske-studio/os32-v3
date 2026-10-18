CHECK_PAR_ORDER += 147:check-cd-module-host
# 記録: tools/tests/module_runtime_tdd.md
# T5b-5: CD registration rollback and mounted-body retention.
check-cd-module-host:
	python3 -B tools/tests/test_cd_module.py $(MUT)

.PHONY: check-cd-module-host
