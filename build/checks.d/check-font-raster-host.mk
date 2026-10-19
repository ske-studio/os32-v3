CHECK_PAR_ORDER += 093:check-font-raster-host
check-font-raster-host:
	python3 -B tools/tests/test_font_raster.py $(MUT)

.PHONY: check-font-raster-host
