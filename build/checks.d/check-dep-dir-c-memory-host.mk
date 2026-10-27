CHECK_PAR_ORDER += 164:check-dep-dir-c-memory-host
check-dep-dir-c-memory-host:
	python3 -B tools/tests/test_dep_dir_c_memory.py $(MUT)

.PHONY: check-dep-dir-c-memory-host
