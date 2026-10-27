CHECK_PAR_ORDER += 163:check-dep-dir-b-host
check-dep-dir-b-host:
	python3 -B tools/tests/test_dep_dir_b_io.py $(MUT)
	python3 -B tools/tests/test_dep_dir_b_pic.py $(MUT)
	python3 -B tools/tests/test_dep_dir_b_timer.py $(MUT)

.PHONY: check-dep-dir-b-host
