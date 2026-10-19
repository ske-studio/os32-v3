CHECK_PAR_ORDER += 093:check-font-cache-host
check-font-cache-host:
	python3 -B tools/tests/test_font_cache.py $(MUT)

.PHONY: check-font-cache-host
