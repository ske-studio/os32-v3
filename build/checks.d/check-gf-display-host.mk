CHECK_PAR_ORDER += 197:check-gf-display-host
check-gf-display-host:
	python3 -B tools/tests/test_gf_display.py
.PHONY: check-gf-display-host
