CHECK_PAR_ORDER += 169:check-dep-dir-a-l10-host
check-dep-dir-a-l10-host:
	python3 -B tools/tests/test_dep_dir_a_l10.py $(MUT)

.PHONY: check-dep-dir-a-l10-host
