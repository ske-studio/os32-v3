CHECK_PAR_ORDER += 090:check-font-guard-host
.PHONY: check-font-guard-host
check-font-guard-host:
	python3 -B tools/tests/test_font_guard.py $(MUT)
