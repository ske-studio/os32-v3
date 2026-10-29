CHECK_PAR_ORDER += 222:check-src-layout-host
check-src-layout-host:
	python3 -B tools/tests/test_src_layout.py $(MUT)
.PHONY: check-src-layout-host
