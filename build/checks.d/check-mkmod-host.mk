CHECK_PAR_ORDER += 137:check-mkmod-host
# 記録: tools/tests/mkmod_tdd.md
check-mkmod-host:
	python3 -B tools/tests/test_mkmod.py $(MUT)
	python3 -B tools/tests/test_kdebug_imports.py $(MUT)
	python3 -B tools/tests/test_exec_selftest.py

.PHONY: check-mkmod-host
