CHECK_PAR_ORDER += 092:check-font-outline-host
check-font-outline-host:
	python3 -B tools/tests/test_font_outline.py $(MUT)

.PHONY: check-font-outline-host
