CHECK_PAR_ORDER += 093:check-font-engine-host
check-font-engine-host:
	python3 -B tools/tests/test_font_engine.py $(MUT)

.PHONY: check-font-engine-host
