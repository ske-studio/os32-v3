CHECK_PAR_ORDER += 165:check-dep-dir-d-emergency-host
check-dep-dir-d-emergency-host:
	python3 -B tools/tests/test_dep_dir_d_emergency.py $(MUT)
.PHONY: check-dep-dir-d-emergency-host
